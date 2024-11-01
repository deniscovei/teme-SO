// SPDX-License-Identifier: BSD-3-Clause

#include "block_meta.h"
#include "printf.h"

struct block_meta *head;
struct block_meta *tail;

bool find_preallocation(void)
{
	static bool preallocation_done;

	if (preallocation_done)
		return true;

	for (struct block_meta *curr_block = head; curr_block;
		 curr_block = curr_block->next) {
		if (curr_block->status != STATUS_MAPPED) {
			preallocation_done = true;
			return true;
		}
	}

	return false;
}

bool empty(void)
{
	return head == NULL;
}

void set_block(struct block_meta *block, size_t size, int status, struct block_meta *prev, struct block_meta *next)
{
	block->size = size;
	block->status = status;
	block->prev = prev;
	block->next = next;
}

void set_size(struct block_meta *block, size_t size)
{
	block->size = size;
}

void set_status(struct block_meta *block, int status)
{
	block->status = status;
}

size_t get_size(struct block_meta *block)
{
	return block->size;
}

int get_status(struct block_meta *block)
{
	return block->status;
}

struct block_meta *front(void)
{
	return head;
}

struct block_meta *back(void)
{
	return tail;
}

void emplace_back(struct block_meta *block, size_t size, int status)
{
	if (!head) {
		set_block(block, size, status, NULL, NULL);
		head = block;
		tail = block;
	} else {
		set_block(block, size, status, tail, NULL);
		tail->next = block;
		tail = block;
	}
}

void emplace_front(struct block_meta *block, size_t size, int status)
{
	if (!head) {
		set_block(block, size, status, NULL, NULL);
		head = block;
		tail = block;
	} else {
		set_block(block, size, status, NULL, head);
		head->prev = block;
		head = block;
	}
}

void erase(struct block_meta *block)
{
	if (block == NULL)
		return;

	if (block == head)
		head = block->next;
	else
		block->prev->next = block->next;

	if (block == tail)
		tail = block->prev;
	else
		block->next->prev = block->prev;
}

bool split_block(struct block_meta *block, size_t size)
{
	if (block->size - size > ALIGNED_METADATA_SIZE) {
		struct block_meta *new_block = (void *)block + size;

		set_block(new_block, block->size - size, STATUS_FREE, block, block->next);

		if (!block->next)
			tail = new_block;

		block->size = size;
		block->next = new_block;

		return true;
	} else {
		return false;
	}
}

bool find_block(struct block_meta *block)
{
	if (!block)
		return false;

	for (struct block_meta *curr_block = head; curr_block;
		 curr_block = curr_block->next) {
		if (block == curr_block)
			return true;
	}

	return false;
}

bool coalesce_with_next(struct block_meta *block)
{
	if (!block->next || block->next->status != STATUS_FREE)
		return false;

	block->size += block->next->size;
	block->next = block->next->next;

	if (!block->next)
		tail = block;

	return true;
}

void coalesce_free_blocks(void)
{
	for (struct block_meta *curr_block = head; curr_block;) {
		if (curr_block->status == STATUS_FREE && curr_block->next &&
			curr_block->next->status == STATUS_FREE) {
			curr_block->size += curr_block->next->size;
			curr_block->next = curr_block->next->next;
		} else {
			if (!curr_block->next)
				tail = curr_block;

			curr_block = curr_block->next;
		}
	}
}

struct block_meta *find_best_fit(size_t size)
{
	struct block_meta *best_fit = NULL;

	for (struct block_meta *curr_block = head; curr_block;
		 curr_block = curr_block->next) {
		if (curr_block->status == STATUS_FREE && curr_block->size >= size &&
			(!best_fit || curr_block->size < best_fit->size))
			best_fit = curr_block;
	}

	return best_fit;
}
