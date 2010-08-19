#ifndef SOUND_H
#define SOUND_H

/* TODO Needs to be made 16 bit on any platform. */
typedef unsigned short int SoundData;

int sound_init(void);

void sound_poll(ARMul_State *state);

void sound_setSampleRate(unsigned long period);

#endif
