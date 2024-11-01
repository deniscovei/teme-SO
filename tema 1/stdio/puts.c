#include <internal/io.h>
#include <internal/syscall.h>
#include <internal/types.h>
#include <errno.h>
#include <stdio.h>

int puts(const char *str)
{
    int i = 0;

    for (; str[i]; i++)
        if (write(stdout, str + i, 1) == EOF)
            return EOF;

    if (write(stdout, "\n", 1) == EOF)
        return EOF;

    return 1;
}
