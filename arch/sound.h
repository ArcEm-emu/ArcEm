#ifndef SOUND_H
#define SOUND_H

#include "../c99.h"

extern int Sound_Init(ARMul_State *state);

extern void Sound_Shutdown(ARMul_State *state);

#ifdef SOUND_SUPPORT

#ifdef SYSTEM_SDL
#define SOUND_FUDGERATE_FRAC
#endif

typedef int16_t SoundData;

extern int Sound_BatchSize; /* How many 16*2 sample batches to attempt to deliver to the platform code at once */
extern CycleCount Sound_DMARate; /* How many cycles between DMA fetches */
#ifdef SOUND_FUDGERATE_FRAC
extern uint32_t Sound_FudgeRate; /* New version of Sound_FudgeRate. 8.24 scale factor applied to Sound_DMARate; can be used by host code to fine-tune audio buffer levels */
#else
extern CycleDiff Sound_FudgeRate; /* Old version of Sound_FudgeRate. Adjustment applied to DMA event timing, to increase/decrease time between each event by the given number of cycles. Flawed because it'll will result in uneven timing if there are different numbers of samples consumed per event. */
#endif

typedef enum {
  Stereo_LeftRight, /* Data is ordered with left channel first */
  Stereo_RightLeft  /* Data is ordered with right channel first */
} Sound_StereoSense;

extern Sound_StereoSense eSound_StereoSense;

extern uint32_t Sound_HostRate; /* Rate of host sound system, in 1/1024 Hz. Must be set by host on init. */

/* These calls are made by DispKbdShared when the corresponding registers are updated */
extern void Sound_SoundFreqUpdated(ARMul_State *state);
extern void Sound_StereoUpdated(ARMul_State *state);

/* This call is made to the platform code upon initialisation */
extern int Sound_InitHost(ARMul_State *state);

/* This call is made to the platform code upon exit */
extern void Sound_ShutdownHost(ARMul_State *state);

/* This call is made to the platform code to get a pointer to an output buffer
   destavail must be set to the available space, measured in the number of stereo pairs (i.e. 4 byte units)
*/
extern SoundData *Sound_GetHostBuffer(int32_t *destavail);

/* This call is made to the platform code once the above buffer has been filled
   numSamples is the number of stereo pairs that were placed in the buffer
*/
extern void Sound_HostBuffered(SoundData *buffer,int32_t numSamples);
#endif

#endif
