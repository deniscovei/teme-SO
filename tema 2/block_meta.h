/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "printf.h"

/**
 * @brief Macro that checks if an assertion is true and exits the program if
 * it is.
 *
 * @param assertion The assertion to check.
 * @param call_description A description of the call that was made.
 */
#define DIE(assertion, call_description) \
	do { \
		if (assertion) { \
			fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__); \
			perror(call_description); \
			exit(errno); \
		} \
	} while (0)

/* Structure to hold memory block metadata */
struct block_meta {
	size_t size; // The size of the memory block.
	int status; // The status of the memory block (free or allocated).
	struct block_meta *prev; // Pointer to the previous block in the list.
	struct block_meta *next; // Pointer to the next block in the list.
};

// Pointer to the head of the list of allocated blocks.
extern struct block_meta *head;

// Pointer to the tail of the list of allocated blocks.
extern struct block_meta *tail;

/**
 * @brief This function checks if the preallocation of memory was done.
 *
 * @return true if preallocation was done, false otherwise.
 */
bool find_preallocation();

/**
 * @brief Checks if the memory allocator's list of blocks is empty.
 *
 * @return true if the memory allocator's list of blocks is empty, false
 * otherwise.
 */
bool empty();

/**
 * @brief Sets the metadata of a memory block.
 *
 * @param block Pointer to the block metadata structure.
 * @param size The size of the memory block.
 * @param status The status of the memory block (free or allocated).
 * @param prev Pointer to the previous block in the list.
 * @param next Pointer to the next block in the list.
 */
void set_block(struct block_meta *block, size_t size, int status,
			   struct block_meta *prev, struct block_meta *next);

/**
 * Sets the size of the given block.
 *
 * @param block Pointer to the block metadata.
 * @param size The size to set for the block.
 */
void set_size(struct block_meta *block, size_t size);

/**
 * @brief Sets the status of a memory block.
 *
 * @param block Pointer to the block metadata structure.
 * @param status The status of the memory block (free or allocated).
 */
void set_status(struct block_meta *block, int status);

/**
 * @brief Gets the size of a memory block.
 *
 * @param block Pointer to the block metadata structure.
 *
 * @return The size of the memory block.
 */
size_t get_size(struct block_meta *block);

/**
 * @brief Gets the status of a memory block.
 *
 * @param block Pointer to the block metadata structure.
 *
 * @return The status of the memory block (free or allocated).
 */
int get_status(struct block_meta *block);

/**
 * Returns a pointer to the first block in the memory allocator's list of
 * blocks.
 *
 * @return A pointer to the first block in the memory allocator's list of
 * blocks.
 */
struct block_meta *front();

/**
 * @brief Returns a pointer to the last block in the linked list of block
 * metadata.
 *
 * @return A pointer to the last block in the linked list of block metadata.
 */
struct block_meta *back();

/**
 * @brief Adds a block to the end of the list of allocated blocks.
 *
 * @param block Pointer to the block metadata structure.
 * @param size The size of the memory block.
 * @param status The status of the memory block (free or allocated).
 */
void emplace_back(struct block_meta *block, size_t size, int status);

/**
 * @brief Inserts a new block at the beginning of the linked list.
 * 
 * @param block Pointer to the block to be inserted.
 * @param size Size of the block to be inserted.
 * @param status Status of the block to be inserted.
 */
void emplace_front(struct block_meta *block, size_t size, int status);

/**
 * @brief Removes a block from the list of allocated blocks.
 *
 * @param block Pointer to the block metadata structure.
 */
void erase(struct block_meta *block);

/**
 * Splits a block into two blocks, one with the requested size and the other
 * with the remaining size.
 * 
 * @param block Pointer to the block to be split.
 * @param size The size of the first block after the split.
 * 
 * @return True if the block was successfully split, false otherwise.
 */
bool split_block(struct block_meta *block, size_t size);

/**
 * @brief Searches for a block of memory with a given address in the list of
 * allocated blocks.
 *
 * @param block A pointer to the block_meta structure representing the block of
 * memory to search for.
 *
 * @return true if a block with the specified address is found, false otherwise.
 */
bool find_block(struct block_meta *block);

/**
 * @brief Coalesces the given block with the next block if it is free.
 * 
 * @block: Pointer to the block to be coalesced
 *
 * @returns true if the coalescing was successful, false otherwise.
 */
bool coalesce_with_next(struct block_meta *block);

/**
 * @brief Coalesces all adjacent free blocks in the memory pool.
 */
void coalesce_free_blocks();

/**
 * @brief Finds the best fit block for a given size.
 *
 * @param size The size of the memory block.
 *
 * @return A pointer to the block metadata structure representing the best fit
 * block.
 */
struct block_meta *find_best_fit(size_t size);

#define ALIGNMENT 8

// aligns size to the next multiple of ALIGNMENT
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

#define ALIGNED_METADATA_SIZE ALIGN(sizeof(struct block_meta))

#define MMAP_THRESHOLD (128 * 1024)

/* Block metadata status values */
#define STATUS_FREE   0
#define STATUS_ALLOC  1
#define STATUS_MAPPED 2
