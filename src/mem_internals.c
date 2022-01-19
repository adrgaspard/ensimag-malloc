/******************************************************
 * Copyright Grégory Mounié 2018                      *
 * This code is distributed under the GLPv3+ licence. *
 * Ce code est distribué sous la licence GPLv3+.      *
 ******************************************************/

#include <sys/mman.h>
#include <assert.h>
#include <stdint.h>
#include "mem.h"
#include "mem_internals.h"

unsigned long knuth_mmix_one_round(unsigned long in) {
    return in * 6364136223846793005UL % 1442695040888963407UL;
}

void *mark_memarea_and_get_user_ptr(void *ptr, unsigned long size, MemKind k) {
    // Validation.
    assert(ptr != NULL);
    assert(k == SMALL_KIND || k == MEDIUM_KIND || k == LARGE_KIND);
    // Define MMIX value and replace it's 2 LSB by the kind value.
    uint64_t magic_value = knuth_mmix_one_round((uint64_t) ptr);
    magic_value &= ~(0b11UL);
    magic_value += k;
    // Write memory area informations.
    *((uint64_t *) ((uint64_t) ptr + 0 * sizeof(uint64_t))) = size;
    *((uint64_t *) ((uint64_t) ptr + 1 * sizeof(uint64_t))) = magic_value;
    *((uint64_t *) ((uint64_t) ptr + size - 2 * sizeof(uint64_t))) = magic_value;
    *((uint64_t *) ((uint64_t) ptr + size - 1 * sizeof(uint64_t))) = size;
    return (void *) ((uint64_t) ptr) + 2 * sizeof(uint64_t);
}

Alloc mark_check_and_get_alloc(void *ptr) {
    // Validation.
    assert(ptr != NULL);
    // Find values.
    uint64_t magic_value_start = *((uint64_t *) ((uint64_t) ptr - 1 * sizeof(uint64_t)));
    uint64_t size_start = *((uint64_t *) ((uint64_t) ptr - 2 * sizeof(uint64_t)));
    uint64_t magic_value_end = *((uint64_t *) ((uint64_t) ptr + size_start - 4 * sizeof(uint64_t)));
    uint64_t size_end = *((uint64_t *) ((uint64_t) ptr + size_start - 3 * sizeof(uint64_t)));
    MemKind kind = (MemKind) (magic_value_start % 4);
    // Data integrity verifications.
    assert(kind == SMALL_KIND || kind == MEDIUM_KIND || kind == LARGE_KIND);
    assert(magic_value_start == magic_value_end);
    assert(size_start == size_end);
    // Return allocation informations.
    Alloc allocation = {};
    allocation.kind = kind;
    allocation.ptr = (void *) ((uint64_t)ptr - 2 * sizeof(uint64_t));
    allocation.size = size_start;
    return allocation;
}

unsigned long mem_realloc_small() {
    assert(arena.chunkpool == 0);
    unsigned long size = (FIRST_ALLOC_SMALL << arena.small_next_exponant);
    arena.chunkpool = mmap(0,
                           size,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1,
                           0);
    if (arena.chunkpool == MAP_FAILED)
        handle_fatalError("small realloc");
    arena.small_next_exponant++;
    return size;
}

unsigned long mem_realloc_medium() {
    uint32_t indice = FIRST_ALLOC_MEDIUM_EXPOSANT + arena.medium_next_exponant;
    assert(arena.TZL[indice] == 0);
    unsigned long size = (FIRST_ALLOC_MEDIUM << arena.medium_next_exponant);
    assert(size == (1 << indice));
    arena.TZL[indice] = mmap(0,
                             size * 2, // twice the size to allign
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS,
                             -1,
                             0);
    if (arena.TZL[indice] == MAP_FAILED)
        handle_fatalError("medium realloc");
    // align allocation to a multiple of the size
    // for buddy algo
    arena.TZL[indice] += (size - (((intptr_t) arena.TZL[indice]) % size));
    arena.medium_next_exponant++;
    return size; // lie on allocation size, but never free
}

// used for test in buddy algo
unsigned int nb_TZL_entries() {
    int nb = 0;

    for (int i = 0; i < TZL_SIZE; i++)
        if (arena.TZL[i])
            nb++;

    return nb;
}
