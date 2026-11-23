#include "vex.h"
#include "nio/difference_memory.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: DifferenceMemory (difference/nio/difference_memory.c)
 * ============================================================================
 * Per-library MemoryArena owner for Difference, backed by Relational Engine's
 * MemoryPool. Starts at zero OS pages and grows on demand until OOM.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: DifferenceMemory (nio/difference_memory)
 * ============================================================================
 * Lazily creates and shuts down Difference's dedicated MemoryArena.
 * ============================================================================
 */

static MemoryArena *s_differenceArena = nullptr;

/** Lazily create and return Difference's page-pooled MemoryArena. */
MemoryArena *Difference_arena() {
    if (s_differenceArena == nullptr)
        s_differenceArena = MemoryArena();
    return s_differenceArena;
}

/** Destroy Difference's MemoryArena and return all pages to MemoryPool. */
fn Difference_arenaShutdown() {
    if (s_differenceArena != nullptr) {
        MemoryArena_destroy(s_differenceArena);
        s_differenceArena = nullptr;
    }
}
