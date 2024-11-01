// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include <sys/mman.h>
#include <string.h>

void *alloc(size_t size, size_t max_heap_allocation_size)
{
	// if size is 0, return NULL
	if (size == 0)
		return NULL;

	// size of the memory block after allocation
	size_t aligned_size = ALIGNED_METADATA_SIZE + ALIGN(size);
	struct block_meta *block;

	// if the size is greater than or equal to the mmap threshold, allocate a
	// new block
	if (aligned_size >= max_heap_allocation_size) {
		block = MMAP_CALL(aligned_size);
		DIE(block == MAP_FAILED, "mmap failed");

		emplace_front(block, aligned_size, STATUS_MAPPED);
	} else if (!find_preallocation()) {
		// if preallocation was not done, allocate a new block of MMAP_THRESHOLD
		// size and split it into an allocated block of the requested size and a
		// free block of the remaining size
		block = sbrk(MMAP_THRESHOLD);
		DIE(block == BRK_FAILED, "sbrk failed");

		// mapped blocks are placed at the front of the list in order to not
		// interfere with the allocated and free blocks
		emplace_back(block, MMAP_THRESHOLD, STATUS_FREE);
		split_block(block, aligned_size);
		set_status(block, STATUS_ALLOC);
	} else {
		// otherwise, try to find a free block with the best fit
		coalesce_free_blocks();
		struct block_meta *best_fit = find_best_fit(aligned_size);

		// if a free block was found, split it into an allocated block of the
		// requested size and a free block of the remaining size
		if (best_fit) {
			block = best_fit;

			split_block(block, aligned_size);
			set_status(block, STATUS_ALLOC);
		} else if (get_status(back()) == STATUS_FREE) {
			// if the last block is free, expand it
			block = back();
			DIE(sbrk(aligned_size - get_size(block)) == BRK_FAILED,
				"sbrk failed");

			set_size(block, aligned_size);
			set_status(block, STATUS_ALLOC);
		} else {
			// otherwise, allocate a new block
			block = sbrk(aligned_size);
			DIE(block == BRK_FAILED, "sbrk failed");

			// allocated blocks are placed at the back of the list
			emplace_back(block, aligned_size, STATUS_ALLOC);
		}
	}

	return ((void *)block) + ALIGNED_METADATA_SIZE;
}

void *os_malloc(size_t size)
{
	// call alloc and compare the requested size with MMAP_THRESHOLD
	return alloc(size, MMAP_THRESHOLD);
}


void os_free(void *ptr)
{
	struct block_meta *block = ptr - ALIGNED_METADATA_SIZE;

	// if the pointer cannot be freed, return
	if (!ptr || !find_block(block) || get_status(block) == STATUS_FREE)
		return;

	// if the block is mapped, unmap it
	if (get_status(block) == STATUS_MAPPED) {
		erase(block);
		DIE(munmap(block, get_size(block)), "munmap failed");
	} else {
		// otherwise, set the block status to free, but keep it in the list
		set_status(block, STATUS_FREE);
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	// call alloc and compare the requested size with the page size
	void *ptr = alloc(nmemb * size, getpagesize());

	// if alloc failed, return NULL
	if (!ptr)
		return NULL;

	// set the allocated memory to 0
	memset(ptr, 0, nmemb * size);
	return ptr;
}

void *os_realloc(void *ptr, size_t size)
{
	// if size is 0, realloc behaves like free
	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	// if ptr is NULL, realloc behaves like malloc
	if (!ptr)
		return os_malloc(size);

	struct block_meta *block = ptr - ALIGNED_METADATA_SIZE;

	if (!find_block(block) || get_status(block) == STATUS_FREE)
		return NULL;

	// size of the data in the memory block found at address ptr
	size_t ptr_data_size = get_size(block);

	// size of the memory block after reallocation
	size_t aligned_size = ALIGNED_METADATA_SIZE + ALIGN(size);

	// if the memory block is mapped, allocate a new one and copy the data
	if (get_status(block) == STATUS_MAPPED || aligned_size >= MMAP_THRESHOLD) {
		void *new_ptr = os_malloc(size);

		memmove(new_ptr, ptr, MIN(ptr_data_size, size));
		os_free(ptr);
		return new_ptr;
	}

	// try to coalsece with the following free blocks
	bool expanded = false;

	// try to coalesce with the next free blocks while the size is smaller than
	// the requested size
	while (get_size(block) < aligned_size) {
		if (!coalesce_with_next(block))
			break;
		expanded = true;
	}

	// try to expand the block
	if (get_size(block) >= aligned_size) {
		split_block(block, aligned_size);
		set_status(block, STATUS_ALLOC);
		return ptr;
	} else if (block == back() && !expanded) {
		DIE(sbrk(aligned_size - get_size(block)) == BRK_FAILED, "sbrk failed");

		set_size(block, aligned_size);
		set_status(block, STATUS_ALLOC);
		return ptr;
	}

	// if block cannot be expanded, allocate a new one and copy the data
	void *new_ptr = os_malloc(size);

	if (!new_ptr)
		return NULL;

	memmove(new_ptr, ptr, MIN(ptr_data_size, size));
	os_free(ptr);
	return new_ptr;
}
