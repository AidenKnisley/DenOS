#ifndef DENOS_MOUSE_H
#define DENOS_MOUSE_H

#include "types.h"

#define MOUSE_BUTTON_LEFT   0x01u
#define MOUSE_BUTTON_RIGHT  0x02u
#define MOUSE_BUTTON_MIDDLE 0x04u

typedef struct mouse_state {
    i32 x;
    i32 y;
    i32 delta_x;
    i32 delta_y;
    u8 buttons;
    u8 pressed;
    u8 released;
} mouse_state_t;

/* Initializes the legacy PS/2 auxiliary port for polled 3-byte packets. */
int mouse_init(void);
int mouse_is_available(void);
void mouse_set_bounds(i32 width, i32 height);
/* Returns 1 when a complete packet was consumed and fills the latest state. */
int mouse_poll(mouse_state_t *state);
void mouse_get_state(mouse_state_t *state);

#endif
