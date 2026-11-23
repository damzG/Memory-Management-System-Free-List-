#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

/*
 * Memory manager core: a sorted singly linked free list plus the
 * First-Fit, Worst-Fit and Deallocate (with coalescing) algorithms.
 * This module has no dependency on Raylib, so it can be unit tested.
 */

#define MEM_TOTAL        1000   /* size of the simulated memory, in units   */
#define MAX_ALLOCS       64     /* max allocated blocks tracked at one time */
#define MM_FAIL          (-1)   /* no free block is large enough            */
#define MM_TABLE_FULL    (-2)   /* allocation table has no room left        */
#define MM_SCENARIO_COUNT 5

typedef struct Node {
    int address;
    int chunk_size;
    struct Node *next;
} Node;

typedef struct {
    int id;        /* block number shown in the UI (#1, #2, ...) */
    int address;
    int size;
} Allocation;

typedef struct {
    Node *head;                      /* free list, sorted by address */
    Allocation allocs[MAX_ALLOCS];   /* blocks currently handed out  */
    int alloc_count;
    int next_id;
} MemoryManager;

/* ---- Core list algorithms (work directly on a free list) ---------------- */

/* Returns the allocated address, or MM_FAIL. May change *head. */
int first_fit(Node **head, int requested_size);
int worst_fit(Node **head, int requested_size);

/* Inserts the freed block in address order and coalesces with neighbours.
 * Returns the (possibly new) head of the list. */
Node *deallocate(Node *head, int freed_address, int freed_size);

/* ---- Manager: free list + bookkeeping of allocated blocks --------------- */

void mm_init(MemoryManager *mm);     /* one free block [0, MEM_TOTAL]      */
void mm_destroy(MemoryManager *mm);  /* releases every node                */
void mm_reset(MemoryManager *mm);    /* destroy + init                     */

/* Return the allocated address, MM_FAIL, or MM_TABLE_FULL. */
int mm_alloc_first_fit(MemoryManager *mm, int size);
int mm_alloc_worst_fit(MemoryManager *mm, int size);

/* Frees the allocated block that starts at `address`.
 * Returns the size freed, or MM_FAIL if no such allocated block exists
 * (this also catches double frees). */
int mm_free(MemoryManager *mm, int address);

const Allocation *mm_find_alloc(const MemoryManager *mm, int address);

int mm_total_free(const MemoryManager *mm);
int mm_largest_free(const MemoryManager *mm);
int mm_free_count(const MemoryManager *mm);

/* Writes "[addr, size] -> [addr, size] -> NULL" into buf (truncates safely). */
void mm_free_list_to_string(const MemoryManager *mm, char *buf, unsigned long cap);

/* Replaces the state with the given free blocks (sorted, non-touching);
 * every gap becomes an allocated block. Returns 1 on success, 0 if invalid
 * (the manager is left unchanged). */
int mm_setup(MemoryManager *mm, const int *addr, const int *size, int count);

/* Sanity check: free list sorted and fully coalesced, nothing overlaps, and
 * free + allocated blocks cover exactly [0, MEM_TOTAL). Returns 1 if valid. */
int mm_validate(const MemoryManager *mm);

/* ---- Ready-made test scenarios ------------------------------------------ */
int mm_load_scenario(MemoryManager *mm, int index);   /* 0 .. COUNT-1 */
const char *mm_scenario_name(int index);
const char *mm_scenario_hint(int index);

#endif