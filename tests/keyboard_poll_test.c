#include <stdio.h>
#include <string.h>
#include "serial.h"
#include "io.h"

struct key_byte { u8 value; u8 auxiliary; };
static struct key_byte bytes[128];
static unsigned int read_index;
static unsigned int write_index;

static void push(u8 value, int auxiliary) {
    bytes[write_index].value = value;
    bytes[write_index].auxiliary = auxiliary ? 1 : 0;
    write_index++;
}

void denos_test_outb(u16 port, u8 value) { (void)port; (void)value; }
u8 denos_test_inb(u16 port) {
    if (port == 0x64) {
        if (read_index == write_index) return 0;
        return (u8)(1u | (bytes[read_index].auxiliary ? 0x20u : 0u));
    }
    if (port == 0x60 && read_index < write_index) return bytes[read_index++].value;
    return 0;
}
void denos_test_outw(u16 port, u16 value) { (void)port; (void)value; }
u16 denos_test_inw(u16 port) { (void)port; return 0; }

static int check(int ok, const char *message) {
    if (ok) return 1;
    fprintf(stderr, "keyboard polling test FAILED: %s\n", message);
    return 0;
}

int main(void) {
    int key = -1;
    if (!check(!serial_try_getkey(&key), "empty controller should not block or return a key")) return 1;

    push(0x1E, 0); /* A */
    if (!check(serial_try_getkey(&key) && key == 'a', "plain A key mapping")) return 1;

    push(0x2A, 0); /* Left shift down */
    push(0x1E, 0);
    if (!check(serial_try_getkey(&key) && key == 'A', "shifted key mapping")) return 1;
    push(0xAA, 0); /* Left shift release */
    push(0x3A, 0); /* Caps lock */
    push(0x1E, 0);
    if (!check(serial_try_getkey(&key) && key == 'A', "caps lock mapping")) return 1;

    push(0xE0, 0);
    push(0x48, 0);
    if (!check(serial_try_getkey(&key) && key == KEY_UP, "extended arrow key mapping")) return 1;

    /* Mouse bytes can share the controller output queue and must not become
       keyboard characters. The graphics event loop drains them via mouse.c. */
    push(0x09, 1);
    push(0x05, 1);
    push(0xFE, 1);
    push(0x30, 0); /* B */
    if (!check(serial_try_getkey(&key) && key == 'B', "AUX bytes should be ignored before the next keyboard key")) return 1;
    if (!check(!serial_try_getkey(&key), "poll should return empty after consuming the keyboard key")) return 1;


    puts("Keyboard polling tests passed: non-blocking empty reads, shift/caps state, extended keys, AUX-byte coexistence.");
    return 0;
}
