#ifndef DENOS_SPEAKER_H
#define DENOS_SPEAKER_H

#include "types.h"

/* PC speaker tone generator. Call speaker_stop() when the tone is finished. */
int speaker_play_tone(u32 frequency_hz);
void speaker_stop(void);

#endif
