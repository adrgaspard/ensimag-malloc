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


static void fork_block(unsigned int block_to_divide_tzl_index) {
    // Validation.
    assert(block_to_divide_tzl_index > 0 &&
           block_to_divide_tzl_index < FIRST_ALLOC_MEDIUM_EXPOSANT + arena.medium_next_exponant);
    assert(arena.TZL[block_to_divide_tzl_index]);

    // Get new blocks address.
    void *first_block_addr = arena.TZL[block_to_divide_tzl_index];
    void *second_block_addr = (void *) (((uint64_t) first_block_addr) ^
                                        ((uint64_t) block_to_divide_tzl_index - 1UL));

    // Delete the old block from the TZL.
    arena.TZL[block_to_divide_tzl_index] = (void *) (*((uint64_t *) arena.TZL[block_to_divide_tzl_index]));

    // Add the two new blocks to the TZL.
    *((uint64_t *) first_block_addr) = (uint64_t) second_block_addr;
    *((uint64_t *) second_block_addr) = (uint64_t) arena.TZL[block_to_divide_tzl_index - 1];
    arena.TZL[block_to_divide_tzl_index - 1] = first_block_addr;
}

static void merge_block(void *block_addr, uint32_t blocks_to_merge_tzl_index) {
    // Validation.
    assert(block_addr != NULL);
    assert(blocks_to_merge_tzl_index < FIRST_ALLOC_MEDIUM_EXPOSANT + arena.medium_next_exponant - 1 &&
           blocks_to_merge_tzl_index >= 0);
    printf("Block address : %lu\n", (uint64_t) block_addr);

    // Get buddy block address.
    void *buddy_addr = (void *) ((uint64_t) block_addr ^ (uint64_t) blocks_to_merge_tzl_index);
    printf("Buddy address : %lu\n", (uint64_t) buddy_addr);

    // Check whether the buddy is free or not.
    void *tmp = arena.TZL[blocks_to_merge_tzl_index];
    bool buddy_found = false;
    while (tmp != NULL) {
        printf("Tmp : %p\n", tmp);
        if (buddy_addr == tmp) {
            buddy_found = true;
            printf("Buddy is free !\n");
            break;
        }
        tmp = (void *) (*((uint64_t *) tmp));
    }
    if (!buddy_found) {
        printf("Buddy is allocated !\n");
        return;
    }

    // Swap the initial block and his buddy if the buddy has a lower address than the initial block.
    if ((uint64_t) buddy_addr < (uint64_t) block_addr) {
        void *swap = buddy_addr;
        buddy_addr = block_addr;
        block_addr = swap;
        printf("Swap ! Block address : %lu & Buddy address : %lu\n", (uint64_t) block_addr,
               (uint64_t) buddy_addr);
    }

    // Delete two blocks from the TZL.
    bool has_deleted_current = false;
    uint8_t nb_blocks_remaining = 2;
    void *previous = NULL;
    void *current = arena.TZL[blocks_to_merge_tzl_index];
    void *next = NULL;
    printf("Deleting old blocks...\n");
    while (current != NULL && nb_blocks_remaining > 0) {
        has_deleted_current = false;
        next = (void *) (*((uint64_t *) current));
        printf("Previous : %p / Current : %p / Next : %p\n", previous, current, next);
        if (current == block_addr || current == buddy_addr) {
            if (previous) {
                *((uint64_t *) previous) = (uint64_t) next;
            } else {
                arena.TZL[blocks_to_merge_tzl_index] = next;
            }
            nb_blocks_remaining--;
            has_deleted_current = true;
        }
        if (has_deleted_current) {
            previous = current;
        }
        current = next;
    }
    assert(nb_blocks_remaining == 0);

    // Add the merged block to the TZL.
    *((uint64_t *) block_addr) = (uint64_t) arena.TZL[blocks_to_merge_tzl_index + 1];
    arena.TZL[blocks_to_merge_tzl_index + 1] = block_addr;
    if (blocks_to_merge_tzl_index < FIRST_ALLOC_MEDIUM_EXPOSANT + arena.medium_next_exponant - 2) {
        printf("--------------------- New call with index %u ---------------------\n", blocks_to_merge_tzl_index + 1);
        merge_block(block_addr, blocks_to_merge_tzl_index + 1);
    }
}

static void *get_tzl_block(unsigned int tzl_index, bool allocate) {
    if (!arena.TZL[tzl_index]) {
        if (tzl_index == FIRST_ALLOC_MEDIUM_EXPOSANT + arena.medium_next_exponant - 1) {
            mem_realloc_medium();
        } else {
            get_tzl_block(tzl_index + 1, false);
        }
        fork_block(tzl_index + 1);
    }
    void *block = arena.TZL[tzl_index];
    if (allocate) {
        arena.TZL[tzl_index] = (void *) (*((uint64_t *) block));
    }
    return block;
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

    // Allocate memory.
    uint64_t real_size = size + 32;
    unsigned int tzl_index = puiss2(real_size);
    void *chunk = get_tzl_block(tzl_index, true);
    printf("TZL Index on alloc : %u\n", tzl_index);
    return mark_memarea_and_get_user_ptr(chunk, real_size, MEDIUM_KIND);
}

void efree_medium(Alloc a) {
    // Validation.
    assert(a.kind == MEDIUM_KIND);
    assert(a.size < LARGEALLOC);
    assert(a.size > SMALLALLOC);

    // Free memory.
    unsigned int tzl_index = puiss2(a.size + 32);
    printf("TZL Index on free : %u\n", tzl_index);
    printf("Size start %lu / Size end %lu / Magic value start %lu / Magic value end %lu / Kind %d\n",
           *((uint64_t *) ((uint64_t) a.ptr + 0UL)),
           *((uint64_t *) ((uint64_t) a.ptr + a.size - 8UL)),
           *((uint64_t *) ((uint64_t) a.ptr + 8UL)),
           *((uint64_t *) ((uint64_t) a.ptr + a.size - 16UL)), (int) a.kind);
    merge_block((void *) ((uint64_t) a.ptr - 16UL), tzl_index);
}
