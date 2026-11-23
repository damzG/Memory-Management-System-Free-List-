#include "memory_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static Node *create_node(int address, int size, Node *next)
{
    Node *n = (Node *)malloc(sizeof *n);
    if (n == NULL) return NULL;
    n->address = address;
    n->chunk_size = size;
    n->next = next;
    return n;
}

static void free_list(Node *head)
{
    while (head != NULL) {
        Node *next = head->next;
        free(head);
        head = next;
    }
}

/* Removes `node` (whose predecessor is `prev`, or NULL for the head). */
static void unlink_node(Node **head, Node *prev, Node *node)
{
    if (prev == NULL) *head = node->next;   /* removing the head node */
    else prev->next = node->next;           /* bypass the node        */
    free(node);
}

/* ------------------------------------------------------------------------ */
/* Core algorithms                                                          */
/* ------------------------------------------------------------------------ */

int first_fit(Node **head, int requested_size)
{
    if (requested_size <= 0) return MM_FAIL;

    Node *current = *head;
    Node *prev = NULL;

    while (current != NULL) {
        if (current->chunk_size >= requested_size) {
            int allocated_address = current->address;

            if (current->chunk_size == requested_size) {
                unlink_node(head, prev, current);          /* exact fit */
            } else {
                current->address += requested_size;        /* split     */
                current->chunk_size -= requested_size;
            }
            return allocated_address;
        }
        prev = current;
        current = current->next;
    }
    return MM_FAIL;
}

int worst_fit(Node **head, int requested_size)
{
    if (requested_size <= 0) return MM_FAIL;

    Node *current = *head;
    Node *prev = NULL;
    Node *best_node = NULL;
    Node *best_prev = NULL;

    /* Step 1: scan the whole list for the largest block that fits.
     * Strict ">" means ties go to the block closest to the head. */
    while (current != NULL) {
        if (current->chunk_size >= requested_size) {
            if (best_node == NULL || current->chunk_size > best_node->chunk_size) {
                best_node = current;
                best_prev = prev;
            }
        }
        prev = current;
        current = current->next;
    }

    /* Step 2: nothing fits */
    if (best_node == NULL) return MM_FAIL;

    int allocated_address = best_node->address;

    /* Step 3: exact fit removes the node, otherwise split it */
    if (best_node->chunk_size == requested_size) {
        unlink_node(head, best_prev, best_node);
    } else {
        best_node->address += requested_size;
        best_node->chunk_size -= requested_size;
    }
    return allocated_address;
}

Node *deallocate(Node *head, int freed_address, int freed_size)
{
    Node *new_node = create_node(freed_address, freed_size, NULL);
    if (new_node == NULL) return head;   /* out of host memory: list unchanged */

    /* Case 1: empty list, or the block belongs before the current head */
    if (head == NULL || new_node->address < head->address) {
        new_node->next = head;
        head = new_node;

        /* coalesce with the old head if they touch */
        if (head->next != NULL &&
            head->address + head->chunk_size == head->next->address) {
            Node *old = head->next;
            head->chunk_size += old->chunk_size;
            head->next = old->next;
            free(old);
        }
        return head;
    }

    /* Case 2: find the node after which the block must be inserted */
    Node *current = head;
    while (current->next != NULL && current->next->address < new_node->address) {
        current = current->next;
    }

    new_node->next = current->next;
    current->next = new_node;

    /* Coalesce with the NEXT block */
    if (new_node->next != NULL &&
        new_node->address + new_node->chunk_size == new_node->next->address) {
        Node *old = new_node->next;
        new_node->chunk_size += old->chunk_size;
        new_node->next = old->next;
        free(old);
    }

    /* Coalesce with the PREVIOUS block */
    if (current->address + current->chunk_size == new_node->address) {
        current->chunk_size += new_node->chunk_size;
        current->next = new_node->next;
        free(new_node);
    }

    return head;
}

/* ------------------------------------------------------------------------ */
/* Manager                                                                  */
/* ------------------------------------------------------------------------ */

void mm_init(MemoryManager *mm)
{
    memset(mm, 0, sizeof *mm);
    mm->head = create_node(0, MEM_TOTAL, NULL);
    mm->next_id = 1;
}

void mm_destroy(MemoryManager *mm)
{
    free_list(mm->head);
    mm->head = NULL;
    mm->alloc_count = 0;
}

void mm_reset(MemoryManager *mm)
{
    mm_destroy(mm);
    mm_init(mm);
}

static int record_alloc(MemoryManager *mm, int address, int size)
{
    if (mm->alloc_count >= MAX_ALLOCS) return 0;
    Allocation *a = &mm->allocs[mm->alloc_count++];
    a->id = mm->next_id++;
    a->address = address;
    a->size = size;
    return a->id;
}

int mm_alloc_first_fit(MemoryManager *mm, int size)
{
    if (mm->alloc_count >= MAX_ALLOCS) return MM_TABLE_FULL;
    int address = first_fit(&mm->head, size);
    if (address >= 0) record_alloc(mm, address, size);
    return address;
}

int mm_alloc_worst_fit(MemoryManager *mm, int size)
{
    if (mm->alloc_count >= MAX_ALLOCS) return MM_TABLE_FULL;
    int address = worst_fit(&mm->head, size);
    if (address >= 0) record_alloc(mm, address, size);
    return address;
}

const Allocation *mm_find_alloc(const MemoryManager *mm, int address)
{
    for (int i = 0; i < mm->alloc_count; i++) {
        if (mm->allocs[i].address == address) return &mm->allocs[i];
    }
    return NULL;
}

int mm_free(MemoryManager *mm, int address)
{
    for (int i = 0; i < mm->alloc_count; i++) {
        if (mm->allocs[i].address != address) continue;

        int size = mm->allocs[i].size;
        int before = mm_total_free(mm);
        mm->head = deallocate(mm->head, address, size);

        /* deallocate() cannot report malloc failure, so verify it worked
         * before forgetting the allocation. */
        if (mm_total_free(mm) != before + size) return MM_FAIL;

        memmove(&mm->allocs[i], &mm->allocs[i + 1],
                (size_t)(mm->alloc_count - i - 1) * sizeof mm->allocs[0]);
        mm->alloc_count--;
        return size;
    }
    return MM_FAIL;
}

int mm_total_free(const MemoryManager *mm)
{
    int total = 0;
    for (const Node *n = mm->head; n != NULL; n = n->next) total += n->chunk_size;
    return total;
}

int mm_largest_free(const MemoryManager *mm)
{
    int largest = 0;
    for (const Node *n = mm->head; n != NULL; n = n->next) {
        if (n->chunk_size > largest) largest = n->chunk_size;
    }
    return largest;
}

int mm_free_count(const MemoryManager *mm)
{
    int count = 0;
    for (const Node *n = mm->head; n != NULL; n = n->next) count++;
    return count;
}

void mm_free_list_to_string(const MemoryManager *mm, char *buf, unsigned long cap)
{
    if (cap == 0) return;
    buf[0] = '\0';
    size_t used = 0;

    for (const Node *n = mm->head; n != NULL; n = n->next) {
        char piece[48];
        int len = snprintf(piece, sizeof piece, "[%d, %d] -> ", n->address, n->chunk_size);
        if (len < 0) break;
        /* keep room for the "... NULL" ending */
        if (used + (size_t)len + 10 >= cap) {
            snprintf(buf + used, cap - used, "... NULL");
            return;
        }
        memcpy(buf + used, piece, (size_t)len + 1);
        used += (size_t)len;
    }
    snprintf(buf + used, cap - used, "NULL");
}

int mm_setup(MemoryManager *mm, const int *addr, const int *size, int count)
{
    if (count < 0 || count > MAX_ALLOCS / 2) return 0;

    /* Validate: in range, sorted, not overlapping and not touching
     * (touching blocks would already have been coalesced). */
    int prev_end = -1;
    for (int i = 0; i < count; i++) {
        if (size[i] <= 0 || addr[i] < 0 || addr[i] + size[i] > MEM_TOTAL) return 0;
        if (i > 0 && addr[i] <= prev_end) return 0;
        prev_end = addr[i] + size[i];
    }

    /* Build the new list first so a malloc failure leaves mm untouched. */
    Node *new_head = NULL;
    Node *tail = NULL;
    for (int i = 0; i < count; i++) {
        Node *n = create_node(addr[i], size[i], NULL);
        if (n == NULL) {
            free_list(new_head);
            return 0;
        }
        if (tail != NULL) tail->next = n;
        else new_head = n;
        tail = n;
    }

    free_list(mm->head);
    mm->head = new_head;
    mm->alloc_count = 0;
    mm->next_id = 1;

    /* Every gap between free blocks is memory that is currently allocated. */
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        if (addr[i] > cursor) record_alloc(mm, cursor, addr[i] - cursor);
        cursor = addr[i] + size[i];
    }
    if (cursor < MEM_TOTAL) record_alloc(mm, cursor, MEM_TOTAL - cursor);
    return 1;
}

int mm_validate(const MemoryManager *mm)
{
    unsigned char used[MEM_TOTAL];
    memset(used, 0, sizeof used);

    const Node *prev = NULL;
    for (const Node *n = mm->head; n != NULL; n = n->next) {
        if (n->chunk_size <= 0 || n->address < 0 ||
            n->address + n->chunk_size > MEM_TOTAL) return 0;
        /* also catches unsorted, overlapping and un-coalesced neighbours */
        if (prev != NULL && prev->address + prev->chunk_size >= n->address) return 0;
        for (int i = n->address; i < n->address + n->chunk_size; i++) {
            if (used[i]) return 0;
            used[i] = 1;
        }
        prev = n;
    }

    for (int k = 0; k < mm->alloc_count; k++) {
        const Allocation *a = &mm->allocs[k];
        if (a->size <= 0 || a->address < 0 || a->address + a->size > MEM_TOTAL) return 0;
        for (int i = a->address; i < a->address + a->size; i++) {
            if (used[i]) return 0;
            used[i] = 1;
        }
    }

    for (int i = 0; i < MEM_TOTAL; i++) {
        if (!used[i]) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Test scenarios                                                           */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *hint;
    int count;
    int addr[3];
    int size[3];
} Scenario;

static const Scenario SCENARIOS[MM_SCENARIO_COUNT] = {
    {
        "Empty free list",
        "All 1000 units are allocated, so the free list is just HEAD -> NULL. "
        "Try allocating any size with First-Fit or Worst-Fit: it fails. "
        "Then deallocate address 0 and watch the list refill.",
        0, { 0 }, { 0 }
    },
    {
        "Request too large",
        "Free blocks are 50, 80 and 120 units (250 in total). Request 200 with "
        "either strategy: it fails even though 250 units are free, because no "
        "single block is big enough (external fragmentation).",
        3, { 100, 300, 600 }, { 50, 80, 120 }
    },
    {
        "Exact-fit request",
        "Free blocks are 60, 100 and 150 units. Request 100 with First-Fit: "
        "the 60 block is too small and the 100 block matches exactly, so its "
        "node is removed from the middle of the list. Then request 60 to "
        "remove the head node. (Worst-Fit with 150 is an exact fit too.)",
        3, { 0, 200, 500 }, { 60, 100, 150 }
    },
    {
        "Dealloc and coalesce",
        "Three allocated blocks sit between free ones. Deallocate address 100: "
        "it touches a free block on each side, so all three merge into "
        "[0, 400]. Then free 400 (merges to [0, 800]) and 800 (memory is one "
        "block again).",
        3, { 0, 300, 600 }, { 100, 100, 200 }
    },
    {
        "First-Fit vs Worst-Fit",
        "Free blocks are 120, 300 and 200 units. Request 100: First-Fit takes "
        "the first block that fits (address 100, leaving 20) while Worst-Fit "
        "takes the largest (address 350, leaving 200). Reload this scenario "
        "to try the other strategy.",
        3, { 100, 350, 800 }, { 120, 300, 200 }
    },
};

int mm_load_scenario(MemoryManager *mm, int index)
{
    if (index < 0 || index >= MM_SCENARIO_COUNT) return 0;
    const Scenario *s = &SCENARIOS[index];
    return mm_setup(mm, s->addr, s->size, s->count);
}

const char *mm_scenario_name(int index)
{
    if (index < 0 || index >= MM_SCENARIO_COUNT) return "";
    return SCENARIOS[index].name;
}

const char *mm_scenario_hint(int index)
{
    if (index < 0 || index >= MM_SCENARIO_COUNT) return "";
    return SCENARIOS[index].hint;
}