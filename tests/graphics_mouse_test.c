#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "mouse.h"
#include "types.h"

volatile u8 denos_test_vga[65536];
typedef struct queued_byte { u8 value; u8 auxiliary; } queued_byte_t;
static queued_byte_t queue[4096];
static u32 queue_read;
static u32 queue_write;
static u8 fake_ports[65536];
static int expect_mouse_data;
static int expect_controller_data;
static int next_key = -1;
static int console_resets;
static u64 fake_ticks;

static void push_byte(u8 value, int auxiliary) {
    if (queue_write < (u32)(sizeof(queue) / sizeof(queue[0]))) {
        queue[queue_write].value = value;
        queue[queue_write].auxiliary = auxiliary ? 1 : 0;
        queue_write++;
    }
}

static void push_packet(u8 flags, u8 dx, u8 dy) {
    push_byte(flags, 1);
    push_byte(dx, 1);
    push_byte(dy, 1);
}

void denos_test_outb(u16 port, u8 value) {
    fake_ports[port] = value;
    if (port == 0x64) {
        if (value == 0x20) push_byte(0x00, 0); /* 8042 command byte */
        else if (value == 0xD4) expect_mouse_data = 1;
        else if (value == 0x60) expect_controller_data = 1;
    } else if (port == 0x60) {
        if (expect_mouse_data) {
            expect_mouse_data = 0;
            push_byte(0xFA, 1); /* PS/2 ACK */
        } else if (expect_controller_data) {
            expect_controller_data = 0;
        }
    }
}

u8 denos_test_inb(u16 port) {
    if (port == 0x64) {
        if (queue_read >= queue_write) return 0;
        return (u8)(0x01u | (queue[queue_read].auxiliary ? 0x20u : 0u));
    }
    if (port == 0x60) {
        if (queue_read >= queue_write) return 0;
        return queue[queue_read++].value;
    }
    return fake_ports[port];
}

void denos_test_outw(u16 port, u16 value) { (void)port; (void)value; }
u16 denos_test_inw(u16 port) { (void)port; return 0; }
void serial_video_reset(void) { console_resets++; }
int serial_try_getkey(int *key) {
    if (next_key < 0 || !key) return 0;
    *key = next_key;
    next_key = -1;
    return 1;
}
int serial_getkey(void) { return GFX_KEY_ESCAPE; }
u64 timer_ticks(void) { return ++fake_ticks; }

static int expect(int condition, const char *message) {
    if (condition) return 1;
    fprintf(stderr, "graphics/mouse test FAILED: %s\n", message);
    return 0;
}

static void drain_events(void) {
    for (int i = 0; i < GFX_EVENT_QUEUE_CAPACITY * 4; i++) {
        if (gfx_event_poll() == GFX_EVENT_NONE) break;
    }
}

int main(void) {
    memset((void *)denos_test_vga, 0, sizeof(denos_test_vga));
    if (!expect(mouse_init(), "PS/2 mouse initialization should succeed")) return 1;
    if (!expect(mouse_is_available(), "mouse should be marked available")) return 1;
    if (!expect(gfx_start(), "graphics mode should start")) return 1;
    gfx_clear(BLUE);

    /* Keyboard events are non-blocking and identify the focused window. */
    next_key = 'D';
    if (!expect(gfx_event_poll() == GFX_EVENT_KEY && gfx_event_key() == 'D' &&
                gfx_event_window() == 0 && gfx_event_x() == 160 &&
                gfx_event_y() == 100 && gfx_event_buttons() == 0 &&
                gfx_event_code() == 'D',
                "event_poll should return a complete key event before a window exists")) return 1;
    if (!expect(gfx_event_poll() == GFX_EVENT_NONE,
                "non-blocking event_poll should report an empty queue")) return 1;

    int standalone_id = gfx_window_create(40, 30, 180, 120, "Drag me");
    if (!expect(standalone_id > 0, "window should be created")) return 1;
    drain_events();
    next_key = 'A';
    if (!expect(gfx_event_poll() == GFX_EVENT_KEY && gfx_event_key() == 'A' &&
                gfx_event_window() == standalone_id && gfx_event_type() == GFX_EVENT_KEY,
                "keyboard events should be routed to the focused window")) return 1;

    /* Move the pointer to the title bar, then press and drag the window. */
    push_packet(0x08, (u8)-100, 60); /* (160,100) -> (60,40) */
    if (!expect(gfx_window_wait_key() == GFX_EVENT_MOUSE, "pointer movement should create a mouse event")) return 1;
    if (!expect(gfx_mouse_x() == 60 && gfx_mouse_y() == 40, "mouse coordinates should track PS/2 deltas")) return 1;
    if (!expect(denos_test_vga[40 * GFX_WIDTH + 60] == WHITE, "software cursor should be visible")) return 1;
    if (!expect(denos_test_vga[100 * GFX_WIDTH + 160] == LIGHT_GRAY,
                "cursor movement should restore the previous pointer footprint from the window surface")) return 1;

    push_packet(0x09, 0, 0); /* Left button pressed on title bar. */
    if (!expect(gfx_window_wait_key() == GFX_EVENT_MOUSE, "title-bar press should be reported")) return 1;
    if (!expect(gfx_window_is_dragging(), "title-bar press should begin dragging")) return 1;
    if (!expect(gfx_mouse_pressed() == GFX_MOUSE_LEFT, "left press edge should be exposed to C apps")) return 1;

    push_packet(0x29, 20, (u8)-20); /* Drag down/right by 20 pixels. */
    if (!expect(gfx_window_wait_key() == GFX_EVENT_MOUSE, "drag motion should be reported")) return 1;
    if (!expect(gfx_window_content_x() == 66 && gfx_window_content_y() == 74,
                "window content coordinates should follow a drag")) return 1;

    push_packet(0x08, 0, 0); /* Release left button. */
    if (!expect(gfx_window_wait_key() == GFX_EVENT_MOUSE, "button release should be reported")) return 1;
    if (!expect(!gfx_window_is_dragging(), "release should stop dragging")) return 1;
    if (!expect(gfx_mouse_released() == GFX_MOUSE_LEFT, "left release edge should be exposed")) return 1;

    /* Move to an app button and click it once. */
    push_packet(0x28, 0, (u8)-40); /* y=60 -> y=100 */
    (void)gfx_window_wait_key();
    push_packet(0x29, 0, 0);
    if (!expect(gfx_window_wait_key() == GFX_EVENT_MOUSE, "content click should be reported")) return 1;
    if (!expect(gfx_button_clicked(70, 90, 40, 20) == 1, "button helper should detect a click in its rectangle")) return 1;
    if (!expect(gfx_button_clicked(70, 90, 40, 20) == 0, "button click should be consumed once")) return 1;

    /* Resize from the lower-right grip. The pointer is currently at (80,100). */
    push_packet(0x08, 100, (u8)-40); /* -> (180,140). */
    (void)gfx_window_wait_key();
    push_packet(0x08, 54, (u8)-24); /* -> (234,164), inside the resize grip. */
    (void)gfx_window_wait_key();
    push_packet(0x09, 0, 0);
    if (!expect(gfx_window_wait_key() == GFX_EVENT_MOUSE && gfx_window_is_resizing(),
                "pressing the lower-right grip should start a resize operation")) return 1;
    push_packet(0x29, 20, (u8)-15); /* Increase width by 20 and height by 15. */
    if (!expect(gfx_window_wait_key() == GFX_EVENT_MOUSE &&
                gfx_window_width() == 200 && gfx_window_height() == 135 &&
                gfx_window_is_resizing(),
                "dragging the resize grip should update window dimensions")) return 1;
    push_packet(0x08, 0, 0);
    (void)gfx_window_wait_key();
    if (!expect(!gfx_window_is_resizing() && !gfx_window_is_dragging(),
                "releasing the resize grip should end the resize operation")) return 1;

    int resize_event_count = 0;
    for (int i = 0; i < GFX_EVENT_QUEUE_CAPACITY * 2; i++) {
        int queued_type = (int)gfx_event_poll();
        if (queued_type == GFX_EVENT_NONE) break;
        if (queued_type == GFX_EVENT_RESIZE && gfx_event_window() == standalone_id &&
            gfx_event_x() == 254 && gfx_event_y() == 179 &&
            gfx_event_buttons() == GFX_MOUSE_LEFT)
            resize_event_count++;
    }
    if (!expect(resize_event_count == 1,
                "a resize packet should create exactly one targeted resize event")) return 1;

    /* Click the red title-bar close control after resizing. */
    push_packet(0x08, (u8)-5, 119); /* (254,179) -> (249,60). */
    (void)gfx_window_wait_key();
    push_packet(0x09, 0, 0);
    if (!expect(gfx_window_wait_key() == GFX_KEY_ESCAPE, "clicking the red close control should close the window")) return 1;
    if (!expect(!gfx_window_is_open(), "window should be closed")) return 1;
    if (!expect(denos_test_vga[0] == BLUE, "closing should restore the original desktop framebuffer")) return 1;

    /* Desktop icons receive clicks when no window covers their hit rectangle. */
    if (!expect(gfx_desktop_start(BLUE), "desktop should start after the legacy window closes")) return 1;
    int files_icon = gfx_desktop_add_icon(10, 10, "FILES", YELLOW);
    if (!expect(files_icon > 0, "desktop icon should be registered")) return 1;
    push_packet(0x08, (u8)-127, 0); /* x=249 -> 122 */
    (void)gfx_desktop_wait_event();
    push_packet(0x08, (u8)-92, 35); /* x=30, y=25 */
    if (!expect(gfx_desktop_wait_event() == GFX_EVENT_MOUSE, "desktop pointer motion should be reported")) return 1;
    push_packet(0x09, 0, 0);
    if (!expect(gfx_desktop_wait_event() == GFX_EVENT_MOUSE, "desktop icon click should be reported")) return 1;
    if (!expect(gfx_desktop_icon_clicked() == files_icon, "desktop icon click should return its stable ID")) return 1;

    int first_window = gfx_window_create(50, 30, 160, 120, "First");
    int second_window = gfx_window_create(100, 50, 160, 120, "Second");
    if (!expect(first_window > 0 && second_window > 0 && first_window != second_window,
                "multiple windows should be created with distinct IDs")) return 1;
    if (!expect(gfx_window_count() == 2 && gfx_window_active() == second_window,
                "new windows should focus and move to the front")) return 1;

    drain_events();
    if (!expect(gfx_window_focus_previous() && gfx_window_active() == first_window,
                "focus_previous should select the previous visible window")) return 1;
    if (!expect(gfx_window_focus_next() && gfx_window_active() == second_window,
                "focus_next should select the next visible window")) return 1;
    if (!expect(gfx_event_poll() == GFX_EVENT_FOCUS &&
                gfx_event_window() == first_window &&
                gfx_event_poll() == GFX_EVENT_FOCUS &&
                gfx_event_window() == second_window,
                "focus changes should be queued with the correct window IDs")) return 1;

    gfx_pixel(130, 90, GREEN);
    if (!expect(denos_test_vga[90 * GFX_WIDTH + 130] == GREEN,
                "focused window drawing should update the visible framebuffer")) return 1;
    if (!expect(gfx_window_select(first_window), "background window should be selectable")) return 1;
    gfx_pixel(130, 90, RED);
    if (!expect(denos_test_vga[90 * GFX_WIDTH + 130] == RED,
                "focused window should be composited above its sibling")) return 1;

    /* Make the second window active, then click the exposed first window. */
    if (!expect(gfx_window_select(second_window), "second window should be active before focus test")) return 1;
    push_packet(0x08, 40, (u8)-55); /* (30,25) -> exposed area (70,80). */
    (void)gfx_desktop_wait_event();
    push_packet(0x09, 0, 0);
    if (!expect(gfx_desktop_wait_event() == GFX_EVENT_WINDOW,
                "clicking a background window should report a focus event")) return 1;
    if (!expect(gfx_window_active() == first_window,
                "mouse click should leave focus on the selected window")) return 1;

    if (!expect(gfx_window_select(second_window), "second window should regain focus")) return 1;
    if (!expect(denos_test_vga[90 * GFX_WIDTH + 130] == GREEN,
                "raising the second window should reveal its independent buffer")) return 1;

    /* The title-bar minimize control hides a window; its taskbar button restores it. */
    push_packet(0x08, 120, 10); /* (70,80) -> (190,70) */
    (void)gfx_desktop_wait_event();
    push_packet(0x08, 44, 10);  /* -> (234,60), over the minimize control */
    (void)gfx_desktop_wait_event();
    push_packet(0x09, 0, 0);
    if (!expect(gfx_desktop_wait_event() == GFX_EVENT_WINDOW &&
                gfx_window_is_minimized(second_window) &&
                gfx_window_active() == first_window,
                "clicking minimize should hide the window and focus the next visible one")) return 1;
    push_packet(0x08, (u8)-126, (u8)-128); /* Move into the gap between taskbar buttons. */
    (void)gfx_desktop_wait_event();
    push_packet(0x09, 0, 0);
    if (!expect(gfx_desktop_wait_event() == GFX_EVENT_MOUSE &&
                gfx_window_active() == 0 && gfx_window_is_minimized(second_window),
                "taskbar gaps must not activate a neighboring window")) return 1;
    push_packet(0x08, 22, 0); /* Move onto the second taskbar button. */
    (void)gfx_desktop_wait_event();
    push_packet(0x09, 0, 0);
    if (!expect(gfx_desktop_wait_event() == GFX_EVENT_WINDOW &&
                !gfx_window_is_minimized(second_window) &&
                gfx_window_active() == second_window,
                "taskbar click should restore and focus a minimized window")) return 1;

    gfx_window_close_id(second_window);
    if (!expect(gfx_window_count() == 1 && gfx_window_active() == first_window,
                "closing one window should focus the remaining window")) return 1;
    if (!expect(denos_test_vga[90 * GFX_WIDTH + 130] == RED,
                "closing the top window should reveal the lower window's pixels")) return 1;
    gfx_window_close();
    if (!expect(gfx_window_count() == 0 && gfx_desktop_is_active(),
                "closing all windows should preserve the desktop")) return 1;
    if (!expect(denos_test_vga[0] == BLUE, "desktop wallpaper should remain visible")) return 1;

    gfx_stop();
    if (!expect(console_resets == 1, "graphics shutdown should restore the text console")) return 1;
    puts("Graphics/mouse tests passed: PS/2 decoding, non-blocking key routing, event queue, pointer motion, desktop icons, focus cycling, title-bar dragging/resizing, close/minimize controls, taskbar restore, and private window surfaces.");
    return 0;
}
