#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "types.h"
#include "mouse.h"

volatile u8 denos_test_vga[65536];
static u64 fake_ticks;
static u8 fake_ports[65536];
static int reset_calls;
static int next_key = -1;

void denos_test_outb(u16 port, u8 value) {
    fake_ports[port] = value;
}

u8 denos_test_inb(u16 port) {
    (void)port;
    return 0;
}

void denos_test_outw(u16 port, u16 value) {
    (void)port;
    (void)value;
}

u16 denos_test_inw(u16 port) {
    (void)port;
    return 0;
}

void serial_video_reset(void) { reset_calls++; }
int serial_getkey(void) { return GFX_KEY_ESCAPE; }
int serial_try_getkey(int *key) {
    if (!key || next_key < 0) return 0;
    *key = next_key;
    next_key = -1;
    return 1;
}
int mouse_init(void) { return 0; }
int mouse_is_available(void) { return 0; }
void mouse_set_bounds(i32 width, i32 height) { (void)width; (void)height; }
int mouse_poll(mouse_state_t *state) { (void)state; return 0; }
void mouse_get_state(mouse_state_t *state) {
    if (!state) return;
    state->x = 160; state->y = 100; state->delta_x = 0; state->delta_y = 0;
    state->buttons = 0; state->pressed = 0; state->released = 0;
}
u64 timer_ticks(void) { return ++fake_ticks; }

static int check_pixel(int x, int y, int color, const char *what) {
    int actual = denos_test_vga[y * GFX_WIDTH + x];
    if (actual != color) {
        fprintf(stderr, "graphics test: %s at (%d,%d): expected %d, got %d\n",
                what, x, y, color, actual);
        return 0;
    }
    return 1;
}

int main(void) {
    memset((void *)denos_test_vga, 0, sizeof(denos_test_vga));
    if (!gfx_start()) {
        fputs("graphics test: gfx_start failed\n", stderr);
        return 1;
    }
    gfx_clear(BLUE);
    if (!check_pixel(0, 0, BLUE, "clear top-left") ||
        !check_pixel(319, 199, BLUE, "clear bottom-right")) return 1;

    /* Ordinary one-pixel drawing must not copy the whole VGA screen. */
    denos_test_vga[0] = 0xA5;
    gfx_pixel(10, 10, WHITE);
    if (denos_test_vga[0] != 0xA5 || !check_pixel(10, 10, WHITE,
                                                  "single-pixel update should use a small damage region")) return 1;
    gfx_clear(BLUE);
    if ((u32)gfx_get_pixel(0, 0) != 0x000000AAu || gfx_get_pixel(-1, 0) != 0) {
        fputs("graphics test: pixel query API did not resolve/clamp coordinates\n", stderr);
        return 1;
    }

    /* A frame is composed in RAM and becomes visible only at end_frame. */
    gfx_begin_frame();
    gfx_pixel(4, 4, RED);
    gfx_fill_rect(5, 4, 3, 2, GREEN);
    if (!check_pixel(4, 4, BLUE, "back buffer must not leak a partial frame") ||
        !check_pixel(5, 4, BLUE, "batched rectangle must remain off-screen")) return 1;
    gfx_end_frame();
    if (!check_pixel(4, 4, RED, "end_frame should present the completed frame") ||
        !check_pixel(5, 4, GREEN, "end_frame should present all batched drawing")) return 1;

    gfx_begin_frame();
    gfx_pixel(8, 8, YELLOW);
    gfx_present();
    if (!check_pixel(8, 8, BLUE, "present must not expose an unfinished frame")) return 1;
    gfx_end_frame();
    if (!check_pixel(8, 8, YELLOW, "end_frame should commit a deferred present")) return 1;

    gfx_pixel(4, 5, RED);
    gfx_pixel(-1, 5, WHITE);
    if (!check_pixel(4, 5, RED, "pixel draw") ||
        !check_pixel(0, 5, BLUE, "clipped pixel")) return 1;

    gfx_fill_triangle(10, 10, 30, 10, 20, 30, GREEN);
    if (!check_pixel(20, 20, GREEN, "filled triangle interior") ||
        !check_pixel(4, 20, BLUE, "filled triangle exterior")) return 1;

    static const long polygon[] = { 70, 10, 94, 10, 100, 28, 82, 40, 64, 28 };
    gfx_fill_polygon(polygon, 5, BRIGHT_GREEN);
    if (!check_pixel(82, 22, BRIGHT_GREEN, "filled polygon interior") ||
        !check_pixel(60, 22, BLUE, "filled polygon exterior")) return 1;
    gfx_polygon(polygon, 5, YELLOW);
    if (!check_pixel(82, 10, YELLOW, "polygon outline edge")) return 1;

    gfx_triangle(40, 10, 60, 10, 50, 30, YELLOW);
    if (!check_pixel(50, 10, YELLOW, "triangle edge")) return 1;

    gfx_progress_bar(5, 50, 24, 10, 5, 10, BRIGHT_GREEN, DARK_GRAY);
    if (!check_pixel(7, 55, BRIGHT_GREEN, "progress filled segment") ||
        !check_pixel(20, 55, DARK_GRAY, "progress unfilled segment")) return 1;

    gfx_button(40, 50, 64, 24, "OK", 0);
    if (!check_pixel(40, 50, WHITE, "button highlight")) return 1;

    gfx_text_scale(80, 50, "H", WHITE, 2);
    if (!check_pixel(80, 50, WHITE, "scaled font first column") ||
        !check_pixel(88, 50, WHITE, "scaled font last column")) return 1;


    if (gfx_text_width("DenOS") != 55) {
        fputs("graphics test: UI text width does not match the 11-pixel advance\n", stderr);
        return 1;
    }
    if (gfx_text_wrapped(100, 120, 55, "Hello world", WHITE, 18) != 36) {
        fputs("graphics test: wrapped text did not split into two lines\n", stderr);
        return 1;
    }
    gfx_fill_rect(18, 38, 16, 20, BLUE);
    gfx_text_ui(20, 40, "A", WHITE);
    {
        int changed = 0;
        for (int y = 40; y < 56; y++) {
            for (int x = 20; x < 30; x++) {
                if (denos_test_vga[y * GFX_WIDTH + x] != BLUE) changed++;
            }
        }
        if (changed < 12) {
            fputs("graphics test: anti-aliased UI glyph did not render\n", stderr);
            return 1;
        }
    }

    int standalone_window = gfx_window_create(30, 20, 220, 130, "Graphics Test");
    if (!standalone_window) {
        fputs("graphics test: valid window was rejected\n", stderr);
        return 1;
    }
    gfx_window_minimize();
    if (gfx_window_active() != standalone_window || gfx_window_is_minimized(standalone_window)) {
        fputs("graphics test: standalone window must not become unreachable by minimizing\n", stderr);
        return 1;
    }
    if (gfx_window_content_x() != 36 || gfx_window_content_y() != 44 ||
        gfx_window_content_width() != 208 || gfx_window_content_height() != 100 ||
        gfx_window_width() != 220 || gfx_window_height() != 130 ||
        !gfx_window_is_open()) {
        fputs("graphics test: window content geometry is incorrect\n", stderr);
        return 1;
    }
    /* VGA fallback quantizes true-color RGB into the 6x6x6 compatibility cube. */
    if (!check_pixel(234, 30, 203, "window close control")) return 1;

    gfx_pixel(40, 50, RED);
    gfx_window_redraw();
    if (!check_pixel(40, 50, RED, "window redraw should preserve its client surface")) return 1;

    if (gfx_window_create(300, 190, 40, 20, "Too Small") != 0 ||
        gfx_window_create(-1, 20, 100, 80, "Negative Origin") != 0) {
        fputs("graphics test: invalid window geometry was accepted\n", stderr);
        return 1;
    }

    next_key = GFX_KEY_ESCAPE;
    if (gfx_window_wait_key() != GFX_KEY_ESCAPE || gfx_window_is_open()) {
        fputs("graphics test: Escape did not close the window\n", stderr);
        return 1;
    }
    if (!check_pixel(40, 50, WHITE, "window close restores saved desktop pixel")) return 1;

    /* The desktop keeps its own background and composes independently buffered windows. */
    if (!gfx_desktop_start(BLUE)) {
        fputs("graphics test: desktop startup failed\n", stderr);
        return 1;
    }
    if (gfx_desktop_start(RED)) {
        fputs("graphics test: desktop reinitialization should be rejected\n", stderr);
        return 1;
    }
    if (gfx_desktop_add_icon(0x7FFFFFFFFFFFFFFFLL, 12, "OVERFLOW", WHITE) != 0) {
        fputs("graphics test: overflowing icon geometry should be rejected\n", stderr);
        return 1;
    }
    int icon_id = gfx_desktop_add_icon(12, 12, "FILES", YELLOW);
    if (icon_id <= 0 || !check_pixel(20, 13, YELLOW, "desktop icon surface")) return 1;
    for (int i = 0; i < GFX_MAX_DESKTOP_ICONS - 1; i++) {
        long icon_x = 55 + (i % 6) * 43;
        long icon_y = 12 + (i / 6) * 42;
        if (gfx_desktop_add_icon(icon_x, icon_y, "LONG LABEL", CYAN) <= 0) {
            fputs("graphics test: a desktop icon inside the advertised capacity was rejected\n", stderr);
            return 1;
        }
    }
    if (gfx_desktop_add_icon(12, 100, "NINTH", WHITE) != 0) {
        fputs("graphics test: desktop icon pool accepted more than eight icons\n", stderr);
        return 1;
    }
    gfx_clear(BLUE);
    if (!check_pixel(20, 13, YELLOW, "desktop icon should survive desktop clear") ||
        !check_pixel(41, 185, DARK_GRAY, "taskbar should be redrawn after desktop clear")) return 1;
    gfx_pixel(100, 10, RED);

    int first_id = gfx_window_create(20, 20, 180, 130, "First Window");
    if (!check_pixel(100, 10, RED, "desktop drawing should survive compositor redraw")) return 1;
    int second_id = gfx_window_create(65, 45, 180, 130, "Second Window");
    if (first_id <= 0 || second_id <= 0 || first_id == second_id ||
        gfx_window_count() != 2 || gfx_window_active() != second_id) {
        fputs("graphics test: multiple window creation/focus failed\n", stderr);
        return 1;
    }
    gfx_pixel(100, 90, GREEN); /* Stored in the second window's private surface. */
    if (!check_pixel(100, 90, GREEN, "top window client surface")) return 1;
    if (!gfx_window_select(first_id) || gfx_window_active() != first_id) {
        fputs("graphics test: selecting the first window failed\n", stderr);
        return 1;
    }
    gfx_pixel(100, 90, RED);
    if (!check_pixel(100, 90, RED, "focused window should be composited above its sibling")) return 1;
    if (!gfx_window_select(second_id) || !check_pixel(100, 90, GREEN,
                                                       "raising sibling should restore its own pixels")) return 1;

    /* Focus changes inside a frame must retarget drawing immediately even
       though the compositor waits until end_frame to update the VGA image. */
    gfx_begin_frame();
    if (!gfx_window_select(first_id)) return 1;
    gfx_pixel(105, 95, CYAN);
    if (!gfx_window_select(second_id)) return 1;
    gfx_pixel(105, 95, YELLOW);
    gfx_end_frame();
    if (!check_pixel(105, 95, YELLOW, "frame should present the final focused window") ||
        !gfx_window_select(first_id) ||
        !check_pixel(105, 95, CYAN, "focus switch inside frame must target the first private surface") ||
        !gfx_window_select(second_id) ||
        !check_pixel(105, 95, YELLOW, "second private surface must retain its frame drawing")) return 1;

    gfx_window_close_id(second_id);
    if (gfx_window_count() != 1 || gfx_window_active() != first_id ||
        !check_pixel(100, 90, RED, "closing top window should reveal lower window surface")) return 1;
    gfx_window_close();
    if (gfx_window_count() != 0 || !gfx_desktop_is_active() ||
        !check_pixel(20, 13, YELLOW, "desktop icon should remain after closing all windows") ||
        !check_pixel(100, 10, RED, "desktop app drawing should persist after closing all windows")) return 1;

    int minimize_id = gfx_window_create(28, 26, 150, 100, "Minimize Test");
    if (minimize_id <= 0) return 1;
    gfx_window_minimize();
    if (!gfx_window_is_minimized(minimize_id) || !gfx_window_is_open_id(minimize_id) ||
        gfx_window_is_open() || gfx_window_active() != 0) {
        fputs("graphics test: minimizing should hide, not destroy, the window\n", stderr);
        return 1;
    }
    if (!gfx_window_select(minimize_id) || gfx_window_is_minimized(minimize_id) ||
        gfx_window_active() != minimize_id) {
        fputs("graphics test: selecting a taskbar window should restore it\n", stderr);
        return 1;
    }
    gfx_window_close();

    /* The fixed window pool must reject a fifth window without changing the
       four existing IDs, ordering, or focus. */
    int capacity_ids[GFX_MAX_WINDOWS];
    for (int i = 0; i < GFX_MAX_WINDOWS; i++) {
        capacity_ids[i] = gfx_window_create(i * 80, 0, 80, 50, "Pool");
        if (capacity_ids[i] <= 0) {
            fputs("graphics test: a window inside the advertised capacity was rejected\n", stderr);
            return 1;
        }
    }
    if (gfx_window_count() != GFX_MAX_WINDOWS ||
        gfx_window_create(0, 60, 80, 50, "Overflow") != 0 ||
        gfx_window_count() != GFX_MAX_WINDOWS) {
        fputs("graphics test: window-pool exhaustion did not fail safely\n", stderr);
        return 1;
    }
    for (int i = 0; i < GFX_MAX_WINDOWS; i++) gfx_window_close_id(capacity_ids[i]);
    if (gfx_window_count() != 0) {
        fputs("graphics test: closing pooled windows left stale entries\n", stderr);
        return 1;
    }

    /* The 32-entry event ring drops the oldest event on overflow and keeps
       the newest focus transitions in their original order. */
    int overflow_a = gfx_window_create(20, 20, 100, 80, "Queue A");
    int overflow_b = gfx_window_create(130, 20, 100, 80, "Queue B");
    if (overflow_a <= 0 || overflow_b <= 0) {
        fputs("graphics test: queue-overflow windows could not be created\n", stderr);
        return 1;
    }
    while (gfx_event_poll() != GFX_EVENT_NONE) { }
    for (int i = 0; i < GFX_EVENT_QUEUE_CAPACITY + 8; i++)
        if (!gfx_window_select((i & 1) ? overflow_b : overflow_a)) return 1;
    int queued_focus = 0;
    long final_focus = 0;
    while (gfx_event_poll() != GFX_EVENT_NONE) {
        if (gfx_event_type() != GFX_EVENT_FOCUS) return 1;
        queued_focus++;
        final_focus = gfx_event_window();
    }
    if (queued_focus != GFX_EVENT_QUEUE_CAPACITY || final_focus != overflow_b) {
        fputs("graphics test: event queue overflow policy or ordering is incorrect\n", stderr);
        return 1;
    }
    gfx_window_close_id(overflow_a);
    gfx_window_close_id(overflow_b);

    gfx_stop();
    if (reset_calls != 1) {
        fprintf(stderr, "graphics test: expected one console reset, got %d\n", reset_calls);
        return 1;
    }
    puts("Graphics tests passed: VGA setup, pixel queries, clipped drawing, batched back-buffer presentation, explicit present, triangles/polygons, wrapped and scaled text, widgets, window geometry, desktop composition, focus, minimize/restore, and shutdown.");
    return 0;
}
