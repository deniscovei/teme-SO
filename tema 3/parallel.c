// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

#define NUM_THREADS		4

static atomic_int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;
static pthread_mutex_t graph_mutex;

typedef struct graph_task_arg {
	unsigned int idx;
} graph_task_arg_t;

static void process_node(unsigned int);

void process_node_wrapper(void *arg)
{
	process_node(((graph_task_arg_t *)arg)->idx);
}

static void process_node(unsigned int idx)
{
	os_node_t *node = graph->nodes[idx];

	graph->visited[idx] = DONE;
	sum += node->info;

	for (unsigned int i = 0; i < node->num_neighbours; i++) {
		pthread_mutex_lock(&graph_mutex);
		if (graph->visited[node->neighbours[i]] == NOT_VISITED) {
			graph_task_arg_t *arg = malloc(sizeof(graph_task_arg_t));

			DIE(arg == NULL, "malloc");

			arg->idx = node->neighbours[i];

			os_task_t *task = create_task(process_node_wrapper, arg, free);

			graph->visited[node->neighbours[i]] = PROCESSING;

			enqueue_task(tp, task);
		}
		pthread_mutex_unlock(&graph_mutex);
	}
}

int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);

	DIE(pthread_mutex_init(&graph_mutex, NULL) != 0, "pthread_mutex_init");

	tp = create_threadpool(NUM_THREADS);
	process_node(0);
	wait_for_completion(tp);
	destroy_threadpool(tp);

	DIE(pthread_mutex_destroy(&graph_mutex) != 0, "pthread_mutex_destroy");
	printf("%d", sum);

	return 0;
}
