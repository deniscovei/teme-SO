// SPDX-License-Identifier: BSD-3-Clause

#include "cmd.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#define READ 0
#define WRITE 1

#define OUT_FLAGS (O_WRONLY | O_CREAT | (s->io_flags ? O_APPEND : O_TRUNC))
#define ERR_FLAGS OUT_FLAGS

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	if (setenv("OLDPWD", getcwd(NULL, 0), 1) == -1)
		return false;

	if (dir == NULL || dir->string == NULL || dir->string[0] == '\0')
		return chdir(getenv("HOME"));

	if (!strcmp(dir->string, ".."))
		return chdir("..");

	if (!strcmp(dir->string, "-"))
		return chdir(getenv("OLDPWD"));

	if (access(dir->string, F_OK) == 0)
		return chdir(dir->string);

	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	exit(EXIT_SUCCESS);
	return EXIT_SUCCESS;
}

static bool do_redirect(int flags, const char *filename, int filedes_count,
						...)
{
	int rc;
	int fd;

	fd = open(filename, flags, 0644);
	if (fd < 0)
		return false;

	va_list args;

	va_start(args, filedes_count);
	for (int i = 0; i < filedes_count; i++) {
		int filedes = va_arg(args, int);

		rc = dup2(fd, filedes);
		if (rc < 0)
			return false;
	}
	va_end(args);

	rc = close(fd);
	return rc >= 0;
}

static const char *handle_token(word_t *word)
{
	if (word->expand)
		return getenv(word->string) ? getenv(word->string) : "";
	else
		return word->string;
}

static char *get_value(word_t *token)
{
	if (!token)
		return NULL;

	char *value = strdup(handle_token(token));

	while (token->next_part) {
		token = token->next_part;
		value = realloc(value, strlen(value) + strlen(handle_token(token)) + 1);
		strcat(value, handle_token(token));
	}

	return value;
}

static bool handle_redirections(simple_command_t *s)
{
	char *in_val = get_value(s->in);
	char *out_val = get_value(s->out);
	char *err_val = get_value(s->err);

	int rt = EXIT_SUCCESS;

	if (s->in)
		if (!do_redirect(O_RDONLY, in_val, 1, STDIN_FILENO))
			rt = EXIT_FAILURE;

	if (s->out && s->err && !strcmp(out_val, err_val)) {
		if (!do_redirect(OUT_FLAGS, out_val, 2, STDOUT_FILENO, STDERR_FILENO))
			rt = EXIT_FAILURE;
	} else {
		if (s->out)
			if (!do_redirect(ERR_FLAGS, out_val, 1, STDOUT_FILENO))
				rt = EXIT_FAILURE;

		if (s->err)
			if (!do_redirect(OUT_FLAGS, err_val, 1, STDERR_FILENO))
				rt = EXIT_FAILURE;
	}

	free(in_val);
	free(out_val);
	free(err_val);

	return rt == EXIT_SUCCESS;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	(void)level;
	(void)father;

	/* Sanity checks. */
	if (!s || !s->verb || !s->verb->string)
		return EXIT_FAILURE;

	/* If builtin command, execute the command. */
	if (strcmp(s->verb->string, "cd") == 0) {
		fflush(stdout);
		int rt = EXIT_SUCCESS;
		int out_backup = dup(STDOUT_FILENO);

		if (!handle_redirections(s))
			rt = EXIT_FAILURE;

		rt |= shell_cd(s->params);

		fflush(stdout);
		dup2(out_backup, STDOUT_FILENO);
		close(out_backup);

		return rt;
	}

	if (strcmp(s->verb->string, "exit") == 0 ||
		strcmp(s->verb->string, "quit") == 0) {
		return shell_exit();
	}

	/* If variable assignment, execute the assignment and return
	 * the exit status.
	 */
	if (s->verb && s->verb->next_part && s->verb->next_part->string &&
		s->verb->next_part->string[0] == '=') {
		const char *env_var = s->verb->string;
		char *value = get_value(s->verb->next_part->next_part);

		// Set or unset the environment variable
		if (value[0] == '\0') {
			if (unsetenv(env_var) == -1) {
				perror("unsetenv");
				return EXIT_FAILURE;
			}
		} else {
			if (setenv(env_var, value, 1) == -1) {
				perror("setenv");
				return EXIT_FAILURE;
			}
		}

		free(value);

		return EXIT_SUCCESS;
	}

	/* If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */
	pid_t pid;
	pid_t wait_ret;
	int status;

	char *command_path = get_word(s->verb);
	int argc;
	char **argv = get_argv(s, &argc);

	pid = fork();
	switch (pid) {
	case -1:
		DIE(EXIT_FAILURE, "fork");
		break;

	case 0:
		if (!handle_redirections(s))
			return EXIT_FAILURE;

		execvp(command_path, argv);

		fprintf(stderr, "Execution failed for '%s'\n", command_path);
		exit(EXIT_FAILURE);

	default:
		wait_ret = waitpid(pid, &status, 0);
		DIE(wait_ret < 0, "waitpid");

		free(command_path);
		for (int i = 0; i < argc; i++)
			free(argv[i]);

		free(argv);

		return WEXITSTATUS(status);
	}

	return EXIT_SUCCESS;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
							command_t *father)
{
	int pid1, pid2;
	int status1, status2;

	pid1 = fork();

	if (pid1 < 0) {
		return false;
	} else if (pid1 == 0) {
		parse_command(cmd1, level + 1, father);
		exit(EXIT_SUCCESS);
	}

	pid2 = fork();

	if (pid2 < 0) {
		return false;
	} else if (pid2 == 0) {
		parse_command(cmd2, level + 1, father);
		exit(EXIT_SUCCESS);
	}

	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	return (WIFEXITED(status1) && WIFEXITED(status2)) ? WEXITSTATUS(status2)
													  : -1;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
						command_t *father)
{
	/* Redirect the output of cmd1 to the input of cmd2. */
	int pipe_fd[2];
	int pid1, pid2;
	int status1, status2;

	if (pipe(pipe_fd) == -1) {
		perror("pipe");
		return false;
	}

	pid1 = fork();

	if (pid1 < 0) {
		perror("fork");
		return false;
	} else if (pid1 == 0) {
		close(pipe_fd[READ]);
		dup2(pipe_fd[WRITE], STDOUT_FILENO);
		close(pipe_fd[WRITE]);

		exit(parse_command(cmd1, level + 1, father));
	}

	pid2 = fork();

	if (pid2 < 0) {
		perror("fork");
		return false;
	} else if (pid2 == 0) {
		close(pipe_fd[WRITE]);
		dup2(pipe_fd[READ], STDIN_FILENO);
		close(pipe_fd[READ]);

		exit(parse_command(cmd2, level + 1, father));
	}

	close(pipe_fd[READ]);
	close(pipe_fd[WRITE]);

	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	return (WIFEXITED(status1) && WIFEXITED(status2)) ? WEXITSTATUS(status2)
													  : -1;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	if (!c)
		return EXIT_FAILURE;

	/* Execute a simple command. */
	if (c->op == OP_NONE)
		return parse_simple(c->scmd, level, father);

	int status1, status2;

	switch (c->op) {
	case OP_SEQUENTIAL:
		status1 = parse_command(c->cmd1, level + 1, c);
		status2 = parse_command(c->cmd2, level + 1, c);
		return (status1 == 0) && (status2 == 0) ? 0 : -1;

	case OP_PARALLEL:
		/* Execute the commands simultaneously. */
		return run_in_parallel(c->cmd1, c->cmd2, level, father);

	case OP_CONDITIONAL_NZERO:
		/* Execute the second command only if the first one
		 * returns non zero.
		 */
		if (parse_command(c->cmd1, level, c) != 0)
			return parse_command(c->cmd2, level, c);

		return 0;

	case OP_CONDITIONAL_ZERO:
		/* Execute the second command only if the first one
		 * returns zero.
		 */
		if (parse_command(c->cmd1, level, c) == 0)
			return parse_command(c->cmd2, level, c);

		return 0;

	case OP_PIPE:
		/* Redirect the output of the first command to the
		 * input of the second.
		 */
		return run_on_pipe(c->cmd1, c->cmd2, level, father);

	default:
		return SHELL_EXIT;
	}

	return EXIT_SUCCESS;
}
