// SPDX-License-Identifier: BSD-3-Clause

#include <internal/mm/mem_list.h>
#include <internal/types.h>
#include <internal/essentials.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void *malloc(size_t size)
{
	// get an available address on the heap
	void *start = mmap(NULL, size + sizeof(size_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (start == MAP_FAILED)
		return NULL;

	// store total allocated size on heap for the current malloc call
	*((size_t *)start) = size + sizeof(size_t);

	if (mem_list_add(start + sizeof(size_t), size) == -1) {
		munmap(start, size + sizeof(size_t));
		return NULL;
	}

	// return the requested address
	return start + sizeof(size_t);
}

void *calloc(size_t nmemb, size_t size)
{
	void *start = malloc(nmemb * size);

	// intialize with 0 all bytes of the added block
	memset(start + sizeof(size_t), 0, nmemb * size);

	return start;
}

void free(void *ptr)
{
	if (!mem_list_del(ptr))
		munmap(ptr - sizeof(size_t), *(size_t *)(ptr - sizeof(size_t)));
}

void *realloc(void *ptr, size_t size)
{
	if (!ptr)
		return malloc(size);

	struct mem_list *item = mem_list_find(ptr);

	// no block found at address ptr in the allocated blocks
	if (!item)
		return NULL;

	void *new_start = malloc(size);

	if (!new_start) {
		free(item->start);
		return NULL;
	}

	memcpy(new_start, ptr, MIN(item->len, size));
	free(item->start);

	if (mem_list_add(new_start, size) == -1) {
		munmap(new_start, size);
		return NULL;
	}

	return new_start;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
	return realloc(ptr, nmemb * size);
}
