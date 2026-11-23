/*
 * Menu-driven Memory Management System (Raylib front-end).
 * All allocator logic lives in memory_manager.c; this file only draws the
 * state and turns clicks / key presses into calls on that module.
 */

#include "raylib.h"
#include "memory_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Layout and colours                                                       */
/* ------------------------------------------------------------------------ */

#define SCREEN_W 1180
#define SCREEN_H 760
#define MAIN_X   320
#define MAIN_W   840

#define LOG_CAPACITY 300
#define LOG_LINE_LEN 128
#define LOG_FONT     15
#define LOG_TEXT_W   396

static const Color COL_BG      = { 244, 245, 247, 255 };
static const Color COL_PANEL   = { 255, 255, 255, 255 };
static const Color COL_BORDER  = { 208, 213, 220, 255 };
static const Color COL_TEXT    = {  33,  37,  41, 255 };
static const Color COL_MUTED   = { 108, 117, 125, 255 };
static const Color COL_ACCENT  = {  40, 100, 200, 255 };
static const Color COL_FREE    = {  56, 160, 105, 255 };
static const Color COL_OK      = {  30, 130,  76, 255 };
static const Color COL_ERR     = { 195,  55,  50, 255 };
static const Color COL_HOVER   = { 232, 238, 250, 255 };
static const Color COL_HEAD    = {  60,  64,  70, 255 };

static const Color PALETTE[6] = {
    {  70, 105, 200, 255 }, { 200, 100,  70, 255 }, { 135,  85, 185, 255 },
    { 176, 138,  30, 255 }, {  40, 135, 165, 255 }, { 185,  70, 120, 255 },
};

static Color block_color(int id)
{
    return PALETTE[((id % 6) + 6) % 6];
}

/* ------------------------------------------------------------------------ */
/* Application state                                                        */
/* ------------------------------------------------------------------------ */

typedef enum {
    MODE_DISPLAY,
    MODE_FIRST_FIT,
    MODE_WORST_FIT,
    MODE_DEALLOC,
    MODE_SCENARIO,
    MODE_RESET,
    MODE_COUNT
} Mode;

static const char *MENU_LABELS[MODE_COUNT] = {
    "1. Display Free List",
    "2. Allocate (First-Fit)",
    "3. Allocate (Worst-Fit)",
    "4. Deallocate Memory",
    "5. Test Scenarios",
    "6. Reset Memory",
};

typedef struct {
    MemoryManager mm;
    Mode mode;
    char input[8];
    int input_len;
    int table_scroll;
    int hl_addr, hl_size;     /* region flashed on the memory map */
    double hl_until;
} App;

/* ------------------------------------------------------------------------ */
/* Log (word-wrapped lines shown in the bottom-right panel)                 */
/* ------------------------------------------------------------------------ */

typedef struct {
    char text[LOG_LINE_LEN];
    Color color;
} LogLine;

static LogLine g_log[LOG_CAPACITY];
static int g_log_count = 0;

static void log_clear(void)
{
    g_log_count = 0;
}

static void log_push_line(const char *s, Color c)
{
    if (g_log_count == LOG_CAPACITY) {
        memmove(g_log, g_log + 1, sizeof(LogLine) * (LOG_CAPACITY - 1));
        g_log_count--;
    }
    snprintf(g_log[g_log_count].text, LOG_LINE_LEN, "%s", s);
    g_log[g_log_count].color = c;
    g_log_count++;
}

static void log_add(Color c, const char *fmt, ...)
{
    char text[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    char line[LOG_LINE_LEN] = "";
    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *end = p;
        while (*end && *end != ' ') end++;
        char word[LOG_LINE_LEN];
        size_t wlen = (size_t)(end - p);
        if (wlen >= sizeof word) wlen = sizeof word - 1;
        memcpy(word, p, wlen);
        word[wlen] = '\0';

        char candidate[2 * LOG_LINE_LEN];
        if (line[0]) snprintf(candidate, sizeof candidate, "%s %s", line, word);
        else snprintf(candidate, sizeof candidate, "%s", word);

        if (line[0] && MeasureText(candidate, LOG_FONT) > LOG_TEXT_W) {
            log_push_line(line, c);
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%.*s", LOG_LINE_LEN - 1, candidate);
        }
        p = end;
    }
    if (line[0]) log_push_line(line, c);
}

/* ------------------------------------------------------------------------ */
/* Small UI helpers                                                         */
/* ------------------------------------------------------------------------ */

static bool button(Rectangle r, const char *label, bool selected)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = selected ? COL_ACCENT : (hover ? COL_HOVER : COL_PANEL);
    Color fg = selected ? WHITE : COL_TEXT;
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, selected ? COL_ACCENT : COL_BORDER);
    DrawText(label, (int)r.x + 12, (int)(r.y + (r.height - 18) / 2), 18, fg);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void draw_text_centered(const char *text, Rectangle r, int size, Color c)
{
    int w = MeasureText(text, size);
    DrawText(text, (int)(r.x + (r.width - w) / 2), (int)(r.y + (r.height - size) / 2), size, c);
}

static void draw_chevron(Vector2 tip, int dir_x, int dir_y, Color c)
{
    /* arrow head made of two short lines: dir points where the arrow goes */
    if (dir_x != 0) {
        DrawLineEx(tip, (Vector2){ tip.x - 8 * dir_x, tip.y - 5 }, 2, c);
        DrawLineEx(tip, (Vector2){ tip.x - 8 * dir_x, tip.y + 5 }, 2, c);
    } else {
        DrawLineEx(tip, (Vector2){ tip.x - 5, tip.y - 8 * dir_y }, 2, c);
        DrawLineEx(tip, (Vector2){ tip.x + 5, tip.y - 8 * dir_y }, 2, c);
    }
}

/* ------------------------------------------------------------------------ */
/* Actions (what each menu item does)                                       */
/* ------------------------------------------------------------------------ */

static void set_highlight(App *app, int addr, int size)
{
    app->hl_addr = addr;
    app->hl_size = size;
    app->hl_until = GetTime() + 1.6;
}

static void clear_input(App *app)
{
    app->input[0] = '\0';
    app->input_len = 0;
}

static void action_display(App *app)
{
    char buf[900];
    mm_free_list_to_string(&app->mm, buf, sizeof buf);
    log_add(COL_ACCENT, "Free list: %s", buf);
    if (app->mm.head == NULL) {
        log_add(COL_MUTED, "The free list is empty: no memory is available.");
    } else {
        log_add(COL_MUTED, "%d free block(s), %d of %d units free, largest block %d.",
                mm_free_count(&app->mm), mm_total_free(&app->mm), MEM_TOTAL,
                mm_largest_free(&app->mm));
    }
}

static void action_reset(App *app)
{
    mm_reset(&app->mm);
    clear_input(app);
    app->table_scroll = 0;
    app->hl_until = 0;
    log_add(COL_ACCENT, "Memory reset: one free block [0, %d].", MEM_TOTAL);
}

static void action_scenario(App *app, int index)
{
    if (!mm_load_scenario(&app->mm, index)) {
        log_add(COL_ERR, "Could not load scenario %d.", index + 1);
        return;
    }
    log_clear();
    app->table_scroll = 0;
    app->hl_until = 0;
    log_add(COL_ACCENT, "Scenario %d: %s", index + 1, mm_scenario_name(index));
    log_add(COL_TEXT, "%s", mm_scenario_hint(index));
}

static void action_alloc(App *app, bool worst_fit)
{
    const char *name = worst_fit ? "Worst-Fit" : "First-Fit";
    if (app->input_len == 0) {
        log_add(COL_ERR, "Type a size first.");
        return;
    }
    int size = atoi(app->input);
    if (size <= 0) {
        log_add(COL_ERR, "The size must be at least 1.");
        return;
    }

    int blocks_before = mm_free_count(&app->mm);
    int free_total = mm_total_free(&app->mm);
    int largest = mm_largest_free(&app->mm);

    int addr = worst_fit ? mm_alloc_worst_fit(&app->mm, size)
                         : mm_alloc_first_fit(&app->mm, size);

    if (addr == MM_TABLE_FULL) {
        log_add(COL_ERR, "%s: the allocation table is full (%d blocks). Free something first.",
                name, MAX_ALLOCS);
        return;
    }
    if (addr == MM_FAIL) {
        if (blocks_before == 0) {
            log_add(COL_ERR, "%s: request of %d failed. The free list is empty.", name, size);
        } else if (size > free_total) {
            log_add(COL_ERR, "%s: request of %d failed. Only %d units are free in total.",
                    name, size, free_total);
        } else {
            log_add(COL_ERR,
                    "%s: request of %d failed. %d units are free, but the largest block is only %d "
                    "(external fragmentation).", name, size, free_total, largest);
        }
        return;
    }

    log_add(COL_OK, "%s: allocated %d units at address %d.", name, size, addr);
    if (mm_free_count(&app->mm) < blocks_before) {
        log_add(COL_MUTED, "Exact fit: that node was removed from the free list.");
    } else {
        log_add(COL_MUTED, "The block was split: the remainder stays in the free list.");
    }
    set_highlight(app, addr, size);
    clear_input(app);
}

static void action_dealloc(App *app)
{
    if (app->input_len == 0) {
        log_add(COL_ERR, "Type an address, or click a row in the allocated table.");
        return;
    }
    int addr = atoi(app->input);
    const Allocation *a = mm_find_alloc(&app->mm, addr);
    if (a == NULL) {
        log_add(COL_ERR, "No allocated block starts at address %d.", addr);
        return;
    }

    int id = a->id;      /* copy: the table entry disappears on success */
    int size = a->size;
    int blocks_before = mm_free_count(&app->mm);

    if (mm_free(&app->mm, addr) == MM_FAIL) {
        log_add(COL_ERR, "Could not free the block at address %d.", addr);
        return;
    }

    int blocks_after = mm_free_count(&app->mm);
    log_add(COL_OK, "Freed block #%d: %d units at address %d.", id, size, addr);
    if (blocks_after > blocks_before) {
        log_add(COL_MUTED, "No neighbouring free block touches it: a new node was inserted in address order.");
    } else if (blocks_after == blocks_before) {
        log_add(COL_MUTED, "It touches one free neighbour: the two were coalesced into one node.");
    } else {
        log_add(COL_MUTED, "It touches a free block on both sides: all three were coalesced into one node.");
    }
    set_highlight(app, addr, size);
    clear_input(app);
}

static void select_mode(App *app, Mode mode)
{
    if (mode == MODE_RESET) {
        action_reset(app);
        return;
    }
    if (app->mode != mode) clear_input(app);
    app->mode = mode;
    if (mode == MODE_DISPLAY) action_display(app);
}

static bool mode_uses_input(Mode m)
{
    return m == MODE_FIRST_FIT || m == MODE_WORST_FIT || m == MODE_DEALLOC;
}

static void run_input_action(App *app)
{
    switch (app->mode) {
        case MODE_FIRST_FIT: action_alloc(app, false); break;
        case MODE_WORST_FIT: action_alloc(app, true);  break;
        case MODE_DEALLOC:   action_dealloc(app);      break;
        default: break;
    }
}

/* ------------------------------------------------------------------------ */
/* Input handling                                                           */
/* ------------------------------------------------------------------------ */

static void handle_text_input(App *app)
{
    bool active = mode_uses_input(app->mode);

    int c;
    while ((c = GetCharPressed()) > 0) {            /* always drain the queue */
        if (active && c >= '0' && c <= '9' && app->input_len < 5) {
            app->input[app->input_len++] = (char)c;
            app->input[app->input_len] = '\0';
        }
    }
    if (active && app->input_len > 0 &&
        (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))) {
        app->input[--app->input_len] = '\0';
    }
    if (active && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
        run_input_action(app);
    }
}

static void handle_shortcuts(App *app)
{
    static const int keys[MODE_COUNT] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6 };
    for (int i = 0; i < MODE_COUNT; i++) {
        if (IsKeyPressed(keys[i])) select_mode(app, (Mode)i);
    }
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

static void draw_left_panel(App *app)
{
    DrawRectangle(10, 10, 290, SCREEN_H - 20, COL_PANEL);
    DrawRectangleLinesEx((Rectangle){ 10, 10, 290, SCREEN_H - 20 }, 1, COL_BORDER);

    DrawText("Memory Manager", 24, 22, 24, COL_TEXT);
    DrawText("Menu  (F1 - F6 also work)", 24, 54, 14, COL_MUTED);

    for (int i = 0; i < MODE_COUNT; i++) {
        Rectangle r = { 24, 78.0f + (float)i * 48, 262, 40 };
        if (button(r, MENU_LABELS[i], app->mode == (Mode)i && i != MODE_RESET)) {
            select_mode(app, (Mode)i);
        }
    }

    const float y0 = 384;
    switch (app->mode) {
        case MODE_FIRST_FIT:
        case MODE_WORST_FIT:
        case MODE_DEALLOC: {
            bool is_free = (app->mode == MODE_DEALLOC);
            DrawText(is_free ? "Address of block to free" : "Size to allocate (units)",
                     24, (int)y0, 16, COL_TEXT);

            Rectangle box = { 24, y0 + 26, 262, 40 };
            DrawRectangleRec(box, WHITE);
            DrawRectangleLinesEx(box, 2, COL_ACCENT);
            DrawText(app->input, 34, (int)box.y + 9, 22, COL_TEXT);
            if (((int)(GetTime() * 2)) % 2 == 0) {
                DrawText("|", 36 + MeasureText(app->input, 22), (int)box.y + 9, 22, COL_ACCENT);
            }

            const char *label = is_free ? "Deallocate"
                              : (app->mode == MODE_WORST_FIT ? "Allocate (Worst-Fit)"
                                                             : "Allocate (First-Fit)");
            if (button((Rectangle){ 24, y0 + 78, 262, 40 }, label, false)) run_input_action(app);

            DrawText(is_free ? "Tip: click a row in the\nAllocated Blocks table to\nfill in its address."
                             : "Type a size and press Enter.",
                     24, (int)y0 + 132, 14, COL_MUTED);
            break;
        }
        case MODE_SCENARIO:
            DrawText("Load a test scenario", 24, (int)y0, 16, COL_TEXT);
            for (int i = 0; i < MM_SCENARIO_COUNT; i++) {
                char label[48];
                snprintf(label, sizeof label, "%d. %s", i + 1, mm_scenario_name(i));
                if (button((Rectangle){ 24, y0 + 26 + (float)i * 44, 262, 38 }, label, false)) {
                    action_scenario(app, i);
                }
            }
            break;
        default:
            DrawText("The free list is printed in\nthe log and drawn on the right.",
                     24, (int)y0, 14, COL_MUTED);
            break;
    }

    /* legend */
    DrawRectangle(24, SCREEN_H - 62, 14, 14, COL_FREE);
    DrawText("Free block", 46, SCREEN_H - 62, 14, COL_MUTED);
    DrawRectangle(24, SCREEN_H - 40, 14, 14, block_color(1));
    DrawText("Allocated block (#id)", 46, SCREEN_H - 40, 14, COL_MUTED);
}

static void draw_memory_map(App *app)
{
    DrawText("Memory map", MAIN_X, 14, 20, COL_TEXT);

    Rectangle bar = { MAIN_X, 44, MAIN_W, 60 };
    float scale = bar.width / (float)MEM_TOTAL;
    DrawRectangleRec(bar, COL_BORDER);

    Vector2 mouse = GetMousePosition();
    char hover[96] = "";

    for (int i = 0; i < app->mm.alloc_count; i++) {
        const Allocation *a = &app->mm.allocs[i];
        Rectangle r = { bar.x + (float)a->address * scale, bar.y, (float)a->size * scale, bar.height };
        DrawRectangleRec(r, block_color(a->id));
        DrawRectangleLinesEx(r, 1, COL_PANEL);
        if (r.width >= 34) {
            char t[16];
            snprintf(t, sizeof t, "#%d", a->id);
            draw_text_centered(t, r, 16, WHITE);
        }
        if (CheckCollisionPointRec(mouse, r)) {
            snprintf(hover, sizeof hover, "Block #%d (allocated): addresses %d - %d, %d units",
                     a->id, a->address, a->address + a->size - 1, a->size);
        }
    }
    for (const Node *n = app->mm.head; n != NULL; n = n->next) {
        Rectangle r = { bar.x + (float)n->address * scale, bar.y, (float)n->chunk_size * scale, bar.height };
        DrawRectangleRec(r, COL_FREE);
        DrawRectangleLinesEx(r, 1, COL_PANEL);
        if (r.width >= 30) {
            char t[16];
            snprintf(t, sizeof t, "%d", n->chunk_size);
            draw_text_centered(t, r, 16, WHITE);
        }
        if (CheckCollisionPointRec(mouse, r)) {
            snprintf(hover, sizeof hover, "Free block: addresses %d - %d, %d units",
                     n->address, n->address + n->chunk_size - 1, n->chunk_size);
        }
    }

    if (GetTime() < app->hl_until) {
        Rectangle r = { bar.x + (float)app->hl_addr * scale - 2, bar.y - 2,
                        (float)app->hl_size * scale + 4, bar.height + 4 };
        DrawRectangleLinesEx(r, 3, COL_TEXT);
    }

    DrawText("0", MAIN_X, 108, 14, COL_MUTED);
    const char *end_label = TextFormat("%d", MEM_TOTAL);
    DrawText(end_label, MAIN_X + MAIN_W - MeasureText(end_label, 14), 108, 14, COL_MUTED);
    if (hover[0]) {
        DrawText(hover, MAIN_X + (MAIN_W - MeasureText(hover, 14)) / 2, 108, 14, COL_TEXT);
    }

    DrawText(TextFormat("Free: %d / %d units     Free blocks: %d     Largest free block: %d",
                        mm_total_free(&app->mm), MEM_TOTAL, mm_free_count(&app->mm),
                        mm_largest_free(&app->mm)),
             MAIN_X, 134, 16, COL_TEXT);
}

static void draw_free_list(const App *app)
{
    DrawText("Free list (sorted by address)", MAIN_X, 168, 18, COL_TEXT);

    enum { COLS = 5, MAX_NODES_SHOWN = 12 };
    const float cw = 128, ch = 54, gap_x = 30, row_h = 74;
    const float x0 = MAIN_X, y0 = 198;

    int n = mm_free_count(&app->mm);
    int shown = n > MAX_NODES_SHOWN ? MAX_NODES_SHOWN : n;
    int has_more = n > shown;
    int total_cells = 1 + shown + has_more + 1;      /* HEAD, nodes, [+k more], NULL */

    const Node *node = app->mm.head;
    Rectangle prev = { 0, 0, 0, 0 };

    for (int i = 0; i < total_cells; i++) {
        int col = i % COLS, row = i / COLS;
        Rectangle r = { x0 + (float)col * (cw + gap_x), y0 + (float)row * row_h, cw, ch };

        if (i > 0) {                                  /* arrow from the previous cell */
            if (col > 0) {
                Vector2 a = { prev.x + prev.width, prev.y + ch / 2 };
                Vector2 b = { r.x - 2, r.y + ch / 2 };
                DrawLineEx(a, b, 2, COL_MUTED);
                draw_chevron(b, 1, 0, COL_MUTED);
            } else {                                  /* wrap to the next row */
                float mid_y = prev.y + ch + (row_h - ch) / 2;
                Vector2 a = { prev.x + cw / 2, prev.y + ch };
                Vector2 b = { a.x, mid_y };
                Vector2 c = { r.x + cw / 2, mid_y };
                Vector2 d = { c.x, r.y - 2 };
                DrawLineEx(a, b, 2, COL_MUTED);
                DrawLineEx(b, c, 2, COL_MUTED);
                DrawLineEx(c, d, 2, COL_MUTED);
                draw_chevron(d, 0, 1, COL_MUTED);
            }
        }

        if (i == 0) {
            DrawRectangleRec(r, COL_HEAD);
            draw_text_centered("HEAD", r, 18, WHITE);
        } else if (i <= shown) {
            DrawRectangleRec(r, COL_PANEL);
            DrawRectangleLinesEx(r, 2, COL_FREE);
            DrawText(TextFormat("addr %d", node->address), (int)r.x + 10, (int)r.y + 8, 16, COL_TEXT);
            DrawText(TextFormat("size %d", node->chunk_size), (int)r.x + 10, (int)r.y + 30, 16, COL_TEXT);
            node = node->next;
        } else if (has_more && i == shown + 1) {
            DrawRectangleRec(r, COL_PANEL);
            DrawRectangleLinesEx(r, 1, COL_BORDER);
            draw_text_centered(TextFormat("+%d more", n - shown), r, 16, COL_MUTED);
        } else {
            DrawRectangleRec(r, COL_BORDER);
            draw_text_centered("NULL", r, 18, COL_MUTED);
        }
        prev = r;
    }

    if (n == 0) {
        DrawText("The free list is empty: every unit of memory is allocated.",
                 MAIN_X + 2 * (int)(cw + gap_x) - 4, (int)y0 + 18, 14, COL_ERR);
    }
}

static void draw_alloc_table(App *app)
{
    DrawText("Allocated blocks", MAIN_X, 436, 18, COL_TEXT);

    Rectangle tbl = { MAIN_X, 462, 400, 278 };
    DrawRectangleRec(tbl, COL_PANEL);
    DrawRectangleLinesEx(tbl, 1, COL_BORDER);

    DrawText("Block",   (int)tbl.x + 34,  (int)tbl.y + 6, 15, COL_MUTED);
    DrawText("Address", (int)tbl.x + 130, (int)tbl.y + 6, 15, COL_MUTED);
    DrawText("Size",    (int)tbl.x + 230, (int)tbl.y + 6, 15, COL_MUTED);
    DrawText("End",     (int)tbl.x + 320, (int)tbl.y + 6, 15, COL_MUTED);
    DrawLine((int)tbl.x, (int)tbl.y + 28, (int)(tbl.x + tbl.width), (int)tbl.y + 28, COL_BORDER);

    const int visible = 9;
    int n = app->mm.alloc_count;
    int max_scroll = n > visible ? n - visible : 0;
    Vector2 mouse = GetMousePosition();

    if (CheckCollisionPointRec(mouse, tbl)) app->table_scroll -= (int)GetMouseWheelMove();
    if (app->table_scroll > max_scroll) app->table_scroll = max_scroll;
    if (app->table_scroll < 0) app->table_scroll = 0;

    if (n == 0) {
        DrawText("No allocated blocks.", (int)tbl.x + 14, (int)tbl.y + 44, 16, COL_MUTED);
    }
    for (int i = 0; i < visible && app->table_scroll + i < n; i++) {
        const Allocation a = app->mm.allocs[app->table_scroll + i];   /* copy */
        Rectangle row = { tbl.x, tbl.y + 28 + (float)i * 27, tbl.width, 27 };
        bool hover = CheckCollisionPointRec(mouse, row);
        if (hover) DrawRectangleRec(row, COL_HOVER);

        DrawRectangle((int)row.x + 10, (int)row.y + 6, 14, 14, block_color(a.id));
        DrawText(TextFormat("#%d", a.id),        (int)row.x + 34,  (int)row.y + 5, 16, COL_TEXT);
        DrawText(TextFormat("%d", a.address),    (int)row.x + 130, (int)row.y + 5, 16, COL_TEXT);
        DrawText(TextFormat("%d", a.size),       (int)row.x + 230, (int)row.y + 5, 16, COL_TEXT);
        DrawText(TextFormat("%d", a.address + a.size - 1), (int)row.x + 320, (int)row.y + 5, 16, COL_TEXT);

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            select_mode(app, MODE_DEALLOC);
            snprintf(app->input, sizeof app->input, "%d", a.address);
            app->input_len = (int)strlen(app->input);
        }
    }
    if (n > visible) {
        DrawText(TextFormat("%d-%d of %d (scroll)", app->table_scroll + 1,
                            app->table_scroll + visible, n),
                 (int)tbl.x + 250, 440, 14, COL_MUTED);
    }
}

static void draw_log(void)
{
    DrawText("Log", 740, 436, 18, COL_TEXT);
    Rectangle box = { 740, 462, 420, 278 };
    DrawRectangleRec(box, COL_PANEL);
    DrawRectangleLinesEx(box, 1, COL_BORDER);

    int visible = (int)(box.height - 16) / 20;
    int start = g_log_count > visible ? g_log_count - visible : 0;
    for (int i = start; i < g_log_count; i++) {
        DrawText(g_log[i].text, (int)box.x + 12, (int)box.y + 8 + (i - start) * 20,
                 LOG_FONT, g_log[i].color);
    }
}

/* ------------------------------------------------------------------------ */
/* Main                                                                     */
/* ------------------------------------------------------------------------ */

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "Memory Management System");
    SetTargetFPS(60);

    App app;
    memset(&app, 0, sizeof app);
    mm_init(&app.mm);
    app.mode = MODE_DISPLAY;

    log_add(COL_TEXT, "Welcome! The simulated memory has %d units.", MEM_TOTAL);
    log_add(COL_MUTED, "Pick a menu item on the left. Option 5 loads ready-made test scenarios.");
    action_display(&app);

    while (!WindowShouldClose()) {
        handle_text_input(&app);
        handle_shortcuts(&app);

        BeginDrawing();
        ClearBackground(COL_BG);
        draw_left_panel(&app);
        draw_memory_map(&app);
        draw_free_list(&app);
        draw_alloc_table(&app);
        draw_log();
        EndDrawing();
    }

    mm_destroy(&app.mm);
    CloseWindow();
    return 0;
}