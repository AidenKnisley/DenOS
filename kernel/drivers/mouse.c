#include "mouse.h"
#include "io.h"

#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define PS2_DATA 0x60
#define PS2_OUTPUT_FULL 0x01
#define PS2_INPUT_FULL 0x02
#define PS2_AUX_DATA 0x20
#define PS2_WAIT_LIMIT 100000u

static int mouse_available;
static i32 mouse_bound_width = 320;
static i32 mouse_bound_height = 200;
static mouse_state_t current_state;
static u8 packet[3];
static u8 packet_index;

static int wait_input_empty(void) {
    for (u32 i = 0; i < PS2_WAIT_LIMIT; i++) {
        if (!(inb(PS2_STATUS) & PS2_INPUT_FULL)) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int wait_output_full(u8 *status_out) {
    for (u32 i = 0; i < PS2_WAIT_LIMIT; i++) {
        u8 status = inb(PS2_STATUS);
        if (status & PS2_OUTPUT_FULL) {
            if (status_out) *status_out = status;
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int controller_read_command_byte(u8 *value) {
    u8 status;
    if (!value || !wait_input_empty()) return 0;
    outb(PS2_COMMAND, 0x20);
    if (!wait_output_full(&status)) return 0;
    /* The controller command-byte response is not auxiliary-device data. */
    if (status & PS2_AUX_DATA) return 0;
    *value = inb(PS2_DATA);
    return 1;
}

static int controller_write_command_byte(u8 value) {
    if (!wait_input_empty()) return 0;
    outb(PS2_COMMAND, 0x60);
    if (!wait_input_empty()) return 0;
    outb(PS2_DATA, value);
    return 1;
}

static int mouse_send(u8 value) {
    if (!wait_input_empty()) return 0;
    outb(PS2_COMMAND, 0xD4);
    if (!wait_input_empty()) return 0;
    outb(PS2_DATA, value);
    return 1;
}

static int mouse_wait_ack(void) {
    for (u32 i = 0; i < PS2_WAIT_LIMIT; i++) {
        u8 status = inb(PS2_STATUS);
        if (status & PS2_OUTPUT_FULL) {
            if (!(status & PS2_AUX_DATA)) return 0;
            if (inb(PS2_DATA) == 0xFA) return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int mouse_command(u8 command) {
    return mouse_send(command) && mouse_wait_ack();
}

int mouse_init(void) {
    u8 command_byte;
    mouse_available = 0;
    packet_index = 0;
    current_state.x = mouse_bound_width > 0 ? mouse_bound_width / 2 : 0;
    current_state.y = mouse_bound_height > 0 ? mouse_bound_height / 2 : 0;
    current_state.delta_x = 0;
    current_state.delta_y = 0;
    current_state.buttons = 0;
    current_state.pressed = 0;
    current_state.released = 0;

    if (!wait_input_empty()) return 0;
    outb(PS2_COMMAND, 0xA8); /* Enable the auxiliary PS/2 clock. */

    if (!controller_read_command_byte(&command_byte)) return 0;
    command_byte = (u8)(command_byte & (u8)~0x02u); /* Polling: disable IRQ12. */
    command_byte = (u8)(command_byte & (u8)~0x20u); /* Ensure AUX clock is enabled. */
    if (!controller_write_command_byte(command_byte)) return 0;

    /* Reset the mouse to defaults, then enable packet reporting. */
    if (!mouse_command(0xF6)) return 0;
    if (!mouse_command(0xF4)) return 0;

    mouse_available = 1;
    return 1;
}

int mouse_is_available(void) {
    return mouse_available;
}

void mouse_set_bounds(i32 width, i32 height) {
    if (width < 1 || width > 4096 || height < 1 || height > 4096) return;
    mouse_bound_width = width;
    mouse_bound_height = height;
    if (current_state.x >= width) current_state.x = width - 1;
    if (current_state.y >= height) current_state.y = height - 1;
}

void mouse_get_state(mouse_state_t *state) {
    if (state) *state = current_state;
}

int mouse_poll(mouse_state_t *state) {
    if (!mouse_available) return 0;

    /* Do not steal keyboard bytes: only consume data tagged as AUX by 8042. */
    for (u32 scan = 0; scan < 32; scan++) {
        u8 status = inb(PS2_STATUS);
        if (!(status & PS2_OUTPUT_FULL) || !(status & PS2_AUX_DATA)) return 0;
        u8 value = inb(PS2_DATA);

        if (packet_index == 0) {
            /* Bit 3 is always set in the first byte of a standard PS/2 packet. */
            if (!(value & 0x08u)) continue;
            packet[0] = value;
            packet_index = 1;
            continue;
        }

        packet[packet_index++] = value;
        if (packet_index < 3) continue;
        packet_index = 0;

        u8 flags = packet[0];
        i32 dx = 0;
        i32 dy = 0;
        if (!(flags & 0x40u)) dx = (i32)(signed char)packet[1];
        if (!(flags & 0x80u)) dy = (i32)(signed char)packet[2];

        i32 next_x = current_state.x + dx;
        i32 next_y = current_state.y - dy;
        if (next_x < 0) next_x = 0;
        if (next_y < 0) next_y = 0;
        if (next_x >= mouse_bound_width) next_x = mouse_bound_width - 1;
        if (next_y >= mouse_bound_height) next_y = mouse_bound_height - 1;

        u8 buttons = (u8)(flags & 0x07u);
        current_state.delta_x = next_x - current_state.x;
        current_state.delta_y = next_y - current_state.y;
        current_state.x = next_x;
        current_state.y = next_y;
        current_state.pressed = (u8)(buttons & (u8)~current_state.buttons);
        current_state.released = (u8)(current_state.buttons & (u8)~buttons);
        current_state.buttons = buttons;

        if (state) *state = current_state;
        return 1;
    }
    return 0;
}
