#include "memory_manager.h"

#include <stdio.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("    FAIL line %d: %s\n", __LINE__, #cond);              \
        }                                                                  \
    } while (0)

/* Compares the free list with a flat {addr, size, addr, size, ...} array. */
static int list_is(const MemoryManager *mm, int n, const int *flat)
{
    const Node *node = mm->head;
    for (int i = 0; i < n; i++) {
        if (node == NULL) return 0;
        if (node->address != flat[2 * i] || node->chunk_size != flat[2 * i + 1]) return 0;
        node = node->next;
    }
    return node == NULL;
}

#define LIST_IS(mm, n, ...) list_is((mm), (n), (const int[]){ __VA_ARGS__ })

/* ---- The five recommended tests ----------------------------------------- */

static void test_empty_free_list(void)
{
    printf("Allocate when the free list is empty\n");
    MemoryManager mm;
    mm_init(&mm);
    CHECK(mm_load_scenario(&mm, 0));
    CHECK(mm.head == NULL);
    CHECK(mm_alloc_first_fit(&mm, 10) == MM_FAIL);
    CHECK(mm_alloc_worst_fit(&mm, 10) == MM_FAIL);
    CHECK(mm_validate(&mm));
    CHECK(mm_free(&mm, 0) == MEM_TOTAL);          /* give it all back */
    CHECK(LIST_IS(&mm, 1, 0, 1000));
    mm_destroy(&mm);
}

static void test_request_too_large(void)
{
    printf("Request larger than any available block\n");
    MemoryManager mm;
    mm_init(&mm);
    CHECK(mm_load_scenario(&mm, 1));
    CHECK(mm_total_free(&mm) == 250);
    CHECK(mm_largest_free(&mm) == 120);
    CHECK(mm_alloc_first_fit(&mm, 200) == MM_FAIL);
    CHECK(mm_alloc_worst_fit(&mm, 200) == MM_FAIL);
    CHECK(LIST_IS(&mm, 3, 100, 50, 300, 80, 600, 120));   /* unchanged */
    CHECK(mm_validate(&mm));
    mm_destroy(&mm);
}

static void test_exact_fit(void)
{
    printf("Allocate a block that exactly matches a free block\n");
    MemoryManager mm;
    mm_init(&mm);

    CHECK(mm_load_scenario(&mm, 2));
    CHECK(mm_alloc_first_fit(&mm, 100) == 200);            /* middle node removed */
    CHECK(LIST_IS(&mm, 2, 0, 60, 500, 150));
    CHECK(mm_alloc_first_fit(&mm, 60) == 0);               /* head node removed   */
    CHECK(LIST_IS(&mm, 1, 500, 150));
    CHECK(mm_validate(&mm));

    CHECK(mm_load_scenario(&mm, 2));
    CHECK(mm_alloc_worst_fit(&mm, 150) == 500);            /* tail node removed   */
    CHECK(LIST_IS(&mm, 2, 0, 60, 200, 100));
    CHECK(mm_validate(&mm));
    mm_destroy(&mm);
}

static void test_dealloc_and_coalesce(void)
{
    printf("Deallocate and show how the free list changes\n");
    MemoryManager mm;
    mm_init(&mm);
    CHECK(mm_load_scenario(&mm, 3));
    CHECK(LIST_IS(&mm, 3, 0, 100, 300, 100, 600, 200));

    CHECK(mm_free(&mm, 100) == 200);                       /* merges both sides   */
    CHECK(LIST_IS(&mm, 2, 0, 400, 600, 200));
    CHECK(mm_validate(&mm));

    CHECK(mm_free(&mm, 400) == 200);
    CHECK(LIST_IS(&mm, 1, 0, 800));
    CHECK(mm_free(&mm, 800) == 200);
    CHECK(LIST_IS(&mm, 1, 0, 1000));
    CHECK(mm_validate(&mm));

    CHECK(mm_free(&mm, 800) == MM_FAIL);                   /* double free        */
    CHECK(mm_free(&mm, 7) == MM_FAIL);                     /* never allocated    */
    mm_destroy(&mm);
}

static void test_first_vs_worst(void)
{
    printf("First-Fit and Worst-Fit choose different blocks\n");
    MemoryManager a, b;
    mm_init(&a);
    mm_init(&b);
    CHECK(mm_load_scenario(&a, 4));
    CHECK(mm_load_scenario(&b, 4));

    CHECK(mm_alloc_first_fit(&a, 100) == 100);
    CHECK(LIST_IS(&a, 3, 200, 20, 350, 300, 800, 200));

    CHECK(mm_alloc_worst_fit(&b, 100) == 350);
    CHECK(LIST_IS(&b, 3, 100, 120, 450, 200, 800, 200));

    CHECK(mm_validate(&a));
    CHECK(mm_validate(&b));
    mm_destroy(&a);
    mm_destroy(&b);
}

/* ---- Extra edge cases --------------------------------------------------- */

static void test_split_and_coalesce_variants(void)
{
    printf("Coalescing with previous only, next only, and both\n");
    MemoryManager mm;
    mm_init(&mm);

    /* previous only */
    CHECK(mm_alloc_first_fit(&mm, 100) == 0);
    CHECK(mm_alloc_first_fit(&mm, 100) == 100);
    CHECK(mm_alloc_first_fit(&mm, 100) == 200);
    CHECK(LIST_IS(&mm, 1, 300, 700));
    CHECK(mm_free(&mm, 0) == 100);
    CHECK(LIST_IS(&mm, 2, 0, 100, 300, 700));              /* new head, no merge */
    CHECK(mm_free(&mm, 100) == 100);
    CHECK(LIST_IS(&mm, 2, 0, 200, 300, 700));              /* merged with prev   */
    CHECK(mm_free(&mm, 200) == 100);
    CHECK(LIST_IS(&mm, 1, 0, 1000));                       /* merged with both   */

    /* next only */
    mm_reset(&mm);
    CHECK(mm_alloc_first_fit(&mm, 100) == 0);
    CHECK(mm_alloc_first_fit(&mm, 100) == 100);
    CHECK(mm_alloc_first_fit(&mm, 100) == 200);
    CHECK(mm_free(&mm, 200) == 100);
    CHECK(LIST_IS(&mm, 1, 200, 800));                      /* merged with next   */
    CHECK(mm_free(&mm, 0) == 100);
    CHECK(LIST_IS(&mm, 2, 0, 100, 200, 800));
    CHECK(mm_free(&mm, 100) == 100);
    CHECK(LIST_IS(&mm, 1, 0, 1000));
    CHECK(mm_validate(&mm));
    mm_destroy(&mm);
}

static void test_worst_fit_tie(void)
{
    printf("Worst-Fit tie goes to the first largest block\n");
    MemoryManager mm;
    mm_init(&mm);
    const int addr[] = { 0, 300 };
    const int size[] = { 100, 100 };
    CHECK(mm_setup(&mm, addr, size, 2));
    CHECK(mm_alloc_worst_fit(&mm, 50) == 0);
    CHECK(LIST_IS(&mm, 2, 50, 50, 300, 100));
    mm_destroy(&mm);
}

static void test_invalid_requests(void)
{
    printf("Zero, negative and oversized requests\n");
    MemoryManager mm;
    mm_init(&mm);
    CHECK(mm_alloc_first_fit(&mm, 0) == MM_FAIL);
    CHECK(mm_alloc_worst_fit(&mm, -5) == MM_FAIL);
    CHECK(mm_alloc_first_fit(&mm, MEM_TOTAL + 1) == MM_FAIL);
    CHECK(LIST_IS(&mm, 1, 0, 1000));
    CHECK(mm_alloc_first_fit(&mm, MEM_TOTAL) == 0);        /* whole memory       */
    CHECK(mm.head == NULL);
    CHECK(mm_validate(&mm));
    mm_destroy(&mm);
}

static void test_allocation_table_full(void)
{
    printf("Allocation table full\n");
    MemoryManager mm;
    mm_init(&mm);
    for (int i = 0; i < MAX_ALLOCS; i++) {
        CHECK(mm_alloc_first_fit(&mm, 1) == i);
    }
    CHECK(mm_alloc_first_fit(&mm, 1) == MM_TABLE_FULL);
    CHECK(mm_alloc_worst_fit(&mm, 1) == MM_TABLE_FULL);
    CHECK(mm_validate(&mm));
    mm_destroy(&mm);
}

static void test_setup_rejects_bad_input(void)
{
    printf("mm_setup rejects touching, overlapping and out-of-range blocks\n");
    MemoryManager mm;
    mm_init(&mm);
    const int touch_a[] = { 0, 100 },  touch_s[] = { 100, 50 };
    const int over_a[]  = { 0, 50 },   over_s[]  = { 100, 50 };
    const int range_a[] = { 950 },     range_s[] = { 100 };
    CHECK(mm_setup(&mm, touch_a, touch_s, 2) == 0);
    CHECK(mm_setup(&mm, over_a, over_s, 2) == 0);
    CHECK(mm_setup(&mm, range_a, range_s, 1) == 0);
    CHECK(LIST_IS(&mm, 1, 0, 1000));                       /* unchanged */
    CHECK(mm_validate(&mm));
    mm_destroy(&mm);
}

static void test_free_list_string(void)
{
    printf("Free list string\n");
    MemoryManager mm;
    mm_init(&mm);
    char buf[128];
    mm_free_list_to_string(&mm, buf, sizeof buf);
    CHECK(buf[0] == '[' && buf[1] == '0');
    mm_load_scenario(&mm, 0);
    mm_free_list_to_string(&mm, buf, sizeof buf);
    CHECK(buf[0] == 'N');                                  /* "NULL" */
    char tiny[16];
    mm_load_scenario(&mm, 4);
    mm_free_list_to_string(&mm, tiny, sizeof tiny);        /* must not overflow */
    CHECK(tiny[15] == '\0' || tiny[0] != '\0');
    mm_destroy(&mm);
}

int main(void)
{
    test_empty_free_list();
    test_request_too_large();
    test_exact_fit();
    test_dealloc_and_coalesce();
    test_first_vs_worst();
    test_split_and_coalesce_variants();
    test_worst_fit_tie();
    test_invalid_requests();
    test_allocation_table_full();
    test_setup_rejects_bad_input();
    test_free_list_string();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}