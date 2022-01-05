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

unsigned long knuth_mmix_one_round(unsigned long in)
{
    return in * 6364136223846793005UL % 1442695040888963407UL;
}

void *mark_memarea_and_get_user_ptr(void *ptr, unsigned long size, MemKind k)
{
    // Validation.
    assert(ptr != NULL);
    assert(k == SMALL_KIND || k == MEDIUM_KIND || k == LARGE_KIND);

    // Define MMIX value and replace it's 2 LSB by the kind value.
    unsigned long magic_value = knuth_mmix_one_round((unsigned long)ptr);
    magic_value &= ~(0b11UL);
    magic_value += k;
    
    // Write memory area informations.
    *((unsigned long *)((unsigned long)ptr + 0UL)) = size;
    *((unsigned long *)((unsigned long)ptr + 8UL)) = magic_value;
    *((unsigned long *)((unsigned long)ptr + size - 16UL)) = magic_value;
    *((unsigned long *)((unsigned long)ptr + size - 8UL)) = size;
    return (void *)((unsigned long)ptr + 16UL);
}

Alloc mark_check_and_get_alloc(void *ptr)
{
    // Validation.
    assert(ptr != NULL);

    // Find values before the usable zone.
    unsigned long magic_value_start = *((unsigned long *)((unsigned long)ptr - 8UL));
    unsigned long size_start = *((unsigned long *)((unsigned long)ptr - 16UL));
    unsigned long magic_value_end = *((unsigned long *)((unsigned long)ptr + size_start - 32UL));
    unsigned long size_end = *((unsigned long *)((unsigned long)ptr + size_start - 24UL));
    MemKind kind = (MemKind)(magic_value_start % 4);

    // Data integrity verifications.
    assert(kind == SMALL_KIND || kind == MEDIUM_KIND || kind == LARGE_KIND);
    assert(magic_value_start == magic_value_end);
    assert(size_start == size_end);

    // Return allocation informations.
    Alloc allocation = {};
    allocation.kind = kind;
    allocation.ptr = ptr;
    allocation.size = size_start;
    return allocation;
}

unsigned long
mem_realloc_small()
{
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

unsigned long
mem_realloc_medium()
{
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
    arena.TZL[indice] += (size - (((intptr_t)arena.TZL[indice]) % size));
    arena.medium_next_exponant++;
    return size; // lie on allocation size, but never free
}

// used for test in buddy algo
unsigned int
nb_TZL_entries()
{
    int nb = 0;

    for (int i = 0; i < TZL_SIZE; i++)
        if (arena.TZL[i])
            nb++;

    return nb;
}
