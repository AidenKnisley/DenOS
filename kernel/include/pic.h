#ifndef DENOS_PIC_H
#define DENOS_PIC_H

#include "types.h"

void pic_init(void);
void pic_send_eoi(u8 irq);
void pic_set_mask(u8 irq);
void pic_clear_mask(u8 irq);
u16 pic_get_mask(void);

#endif
