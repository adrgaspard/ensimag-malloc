/******************************************************
 * Copyright Grégory Mounié 2018                      *
 * This code is distributed under the GLPv3+ licence. *
 * Ce code est distribué sous la licence GPLv3+.      *
 ******************************************************/

#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include "mem.h"
#include "mem_internals.h"

static uint64_t get_buddy_value(uint64_t value, uint64_t tzl_index) {
    return value ^ (1 << tzl_index);
}

static void *get_next_block(void *block) {
    uint64_t *tmp = (uint64_t *)block;
    uint64_t next_address = *tmp;
    return (void *) next_address;
}

static void set_next_block(void *block, void *next) {
    uint64_t *tmp = (uint64_t *)block;
    uint64_t next_address = (uint64_t) next;
    *tmp = next_address;
}

static void push_on_tzl_stack(uint64_t tzl_index, void *block) {
    void *next = arena.TZL[tzl_index];
    uint64_t *first = (uint64_t *) block;
    *first = (uint64_t) next;
    arena.TZL[tzl_index] = block;
}

static void *pop_on_tzl_stack(uint64_t tzl_index) {
    void *result = arena.TZL[tzl_index];
    assert(result != NULL);
    arena.TZL[tzl_index] = get_next_block(result);
    set_next_block(result, NULL);
    return result;
}

static bool is_block_in_tzl_stack(uint64_t tzl_index, uint64_t block_address) {
    void *iterator = arena.TZL[tzl_index];
    while (iterator != NULL) {
        if ((uint64_t) iterator == block_address) {
            return true;
        }
        iterator = get_next_block(iterator);
    }
    return false;
}

static void remove_block_from_tzl_stack(uint64_t tzl_index, uint64_t block_address) {
    // Initialization.
    void *previous = NULL;
    void *current = arena.TZL[tzl_index];
    void *next = NULL;
    // Iterate over the chunks.
    while (current != NULL) {
        next = get_next_block(current);
        if ((uint64_t) current == block_address) {
            if (previous == NULL) {
                arena.TZL[tzl_index] = next;
            } else {
                set_next_block(previous, next);
            }
            set_next_block(current, NULL);
            return;
        } else {
            previous = current;
        }
        current = next;
    }
    assert(0);
}

unsigned int puiss2(unsigned long size) {
    unsigned int p = 0;
    size = size - 1; // allocation start in 0
    while (size) { // get the largest bit
        p++;
        size >>= 1;
    }
    if (size > (1 << p))
        p++;
    return p;
}

void *emalloc_medium(unsigned long size) {
    // Validation.
    assert(size < LARGEALLOC);
    assert(size > SMALLALLOC);
    // Find the first index that have at least one block free.
    uint64_t real_size = size + 32;
    uint64_t tzl_index = puiss2(real_size);
    uint64_t iterator_for_find = tzl_index;
    while (arena.TZL[iterator_for_find] == NULL) {
        iterator_for_find++;
        if (iterator_for_find == FIRST_ALLOC_MEDIUM_EXPOSANT + arena.medium_next_exponant) {
            mem_realloc_medium();
            break;
        }
    }
    // Get the first block address and delete it from the TZL.
    uint64_t iterator_for_fork = iterator_for_find;
    uint64_t block_address = (uint64_t) arena.TZL[iterator_for_fork];
    pop_on_tzl_stack(iterator_for_fork);
    // Fork bigger blocks until there is a correctly sized available block.
    while (iterator_for_fork > tzl_index) {
        iterator_for_fork--;
        // Get the buddy block address.
        uint64_t buddy_address = get_buddy_value(block_address, iterator_for_fork);
        // Add the buddy to the TZL.
        push_on_tzl_stack(iterator_for_fork, (void *)buddy_address);
    }
    // Remove the block from the TZL, mark it and return it.
    return mark_memarea_and_get_user_ptr((void *) block_address, real_size, MEDIUM_KIND);
}

void efree_medium(Alloc a) {
    // Validation.
    assert(a.kind == MEDIUM_KIND);
    assert(a.size < LARGEALLOC);
    assert(a.size > SMALLALLOC);
    // Variable initialization.
    uint64_t tzl_index_iterator = puiss2(a.size);
    uint64_t block_address = (uint64_t) a.ptr;
    uint64_t buddy_address;
    // Iteratively merge blocks if needed.
    while (tzl_index_iterator < FIRST_ALLOC_MEDIUM_EXPOSANT + arena.medium_next_exponant - 2) {
        // Get buddy block address.
        buddy_address = get_buddy_value(block_address, tzl_index_iterator);
        if (is_block_in_tzl_stack(tzl_index_iterator, buddy_address)) {
            // Remove the buddy block from the TZL.
            remove_block_from_tzl_stack(tzl_index_iterator, buddy_address);
            // Swap the initial block and his buddy if the buddy has a lower address than the initial block.
            if (block_address > buddy_address) {
                uint64_t tmp_value = block_address;
                block_address = buddy_address;
                buddy_address = tmp_value;
            }
            // Increment the TZL index iterator.
            tzl_index_iterator++;
        } else {
            push_on_tzl_stack(tzl_index_iterator, (void *) block_address);
            break;
        }
    }
}
