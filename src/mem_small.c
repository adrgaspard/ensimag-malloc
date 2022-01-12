/******************************************************
 * Copyright Grégory Mounié 2018                      *
 * This code is distributed under the GLPv3+ licence. *
 * Ce code est distribué sous la licence GPLv3+.      *
 ******************************************************/

#include <assert.h>
#include "mem.h"
#include "mem_internals.h"

void *emalloc_small(unsigned long size) {
    // Validation.
    assert(size > 0 && size <= 64);

    // Create new chunks if needed.
    if (arena.chunkpool == NULL) {
        // Realloc new chunks.
        u_int64_t nb_chunks_reallocated = mem_realloc_small() / CHUNKSIZE;

        // Link chunks between them.
        for (u_int64_t i = 0; i < nb_chunks_reallocated - 1; i++) {
            void **current = (void **) ((u_int64_t) arena.chunkpool + i * CHUNKSIZE);
            void *next = (void *) ((u_int64_t) current + CHUNKSIZE);
            *current = next;
        }

        // Set the last chunk pointer to null.
        *((void **) ((u_int64_t) arena.chunkpool + (nb_chunks_reallocated - 1) * CHUNKSIZE)) = NULL;
    }

    // Take first chunk and mark it.
    void *chunk = arena.chunkpool;
    arena.chunkpool = *((void **) arena.chunkpool);
    return mark_memarea_and_get_user_ptr(chunk, CHUNKSIZE, SMALL_KIND);
}

void efree_small(Alloc a) {
    *((void **) a.ptr) = arena.chunkpool;
    arena.chunkpool = a.ptr;
}
