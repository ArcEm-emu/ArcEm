/* Common sound code */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../armdefs.h"
#include "arch/armarc.h"
#include "arch/sound.h"
#include "../armemu.h"

#define MAX_BATCH_SIZE 1024

int Sound_BatchSize = 1; /* How many DMA fetches to try to do at once */
unsigned long Sound_DMARate; /* How many cycles between DMA fetches */
Sound_StereoSense eSound_StereoSense = Stereo_LeftRight;
int Sound_FudgeRate = 0;

static SoundData soundTable[256];
static ARMword channelAmount[8][2];
unsigned int log2numchan = 0;

static SoundData soundBuffer[16*2*MAX_BATCH_SIZE];

void Sound_UpdateDMARate(ARMul_State *state)
{
  /* Calculate a new value for how often we should trigger a sound DMA fetch
     Relies on:
     VIDC.SoundFreq - the rate of the sound system we're trying to emulate
     ARMul_EmuRate - roughly how many EventQ clock cycles occur per second */
  static unsigned int oldsoundfreq = 0;
  static unsigned long oldemurate = 0;
  if((VIDC.SoundFreq == oldsoundfreq) && (ARMul_EmuRate == oldemurate))
    return;
  oldsoundfreq = VIDC.SoundFreq;
  oldemurate = ARMul_EmuRate;
  /* DMA fetches 16 bytes, at a rate of 1000000/(16*(VIDC.SoundFreq+2)) Hz */
  unsigned long DMArate = 1000000/(16*(VIDC.SoundFreq+2));
  /* Now calculate how many EventQ cycles this is */
  Sound_DMARate = ARMul_EmuRate/DMArate;
//  printf("UpdateDMARate: f %d r %u -> %u\n",VIDC.SoundFreq,ARMul_EmuRate,Sound_DMARate);
}

static void
SoundInitTable(void)
{
  unsigned i;

  for (i = 0; i < 256; i++) {
    /*
     * (not VIDC1, whoops...)
     * VIDC2:
     * 0 Sign
     * 4,3,2,1 Point on chord
     * 7,6,5 Chord select
     */

    unsigned int chordSelect = (i & 0xE0) >> 5;
    unsigned int pointSelect = (i & 0x1E) >> 1;
    unsigned int sign = (i & 1);

    unsigned int stepSize;

    SoundData scale = (SoundData) 0xFFFF / (247 * 2);
    SoundData chordBase;
    SoundData sample;

    switch (chordSelect) {
      case 0: chordBase = 0;
              stepSize = scale / 16;
              break;
      case 1: chordBase = scale;
              stepSize = (2 * scale) / 16;
              break;
      case 2: chordBase = 3*scale;
              stepSize = (4 * scale) / 16;
              break;
      case 3: chordBase = 7*scale;
              stepSize = (8 * scale) / 16;
              break;
      case 4: chordBase = 15*scale;
              stepSize = (16 * scale) / 16;
              break;
      case 5: chordBase = 31*scale;
              stepSize = (32 * scale) / 16;
              break;
      case 6: chordBase = 63*scale;
              stepSize = (64 * scale) / 16;
              break;
      case 7: chordBase = 127*scale;
              stepSize = (120 * scale) / 16;
              break;
      /* End of chord 7 is 247 * scale. */

      default: chordBase = 0;
               stepSize = 0;
               break;
    }

    sample = chordBase + stepSize * pointSelect;

    if (sign == 1) { /* negative */
      soundTable[i] = ~(sample - 1);
    } else {
      soundTable[i] = sample;
    }
    if (i == 128) {
      soundTable[i] = 0xFFFF;
    }
  }
}

/**
 * Sound_StereoUpdated
 *
 * Called whenever VIDC stereo image registers are written so that the
 * channelAmount array can be recalculated and the new number of channels can
 * be figured out.
 */
void Sound_StereoUpdated(ARMul_State *state)
{
  int i = 0;

  /* Note that the order of this if block is important for it to
     pick the right number of channels. */

  /* 1 channel mode? */
  if (VIDC.StereoImageReg[0] == VIDC.StereoImageReg[1] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[2] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[3] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[4] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[5] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[6] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[7])
  {
    log2numchan = 0;
  }
  /* 2 channel mode? */
  else if (VIDC.StereoImageReg[0] == VIDC.StereoImageReg[2] &&
           VIDC.StereoImageReg[0] == VIDC.StereoImageReg[4] &&
           VIDC.StereoImageReg[0] == VIDC.StereoImageReg[6] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[3] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[5] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[7])
  {
    log2numchan = 1;
  }
  /* 4 channel mode? */
  else if (VIDC.StereoImageReg[0] == VIDC.StereoImageReg[4] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[5] &&
           VIDC.StereoImageReg[2] == VIDC.StereoImageReg[6] &&
           VIDC.StereoImageReg[3] == VIDC.StereoImageReg[7])
  {
    log2numchan = 2;
  }
  /* Otherwise it is 8 channel mode. */
  else {
    log2numchan = 3;
  }

//  /* fudge */
//  if(1000000/((VIDC.SoundFreq+2)<<log2numchan) > 50000)
//    log2numchan = 3;

  for (i = 0; i < 8; i++) {
    int reg = VIDC.StereoImageReg[i];
    if(eSound_StereoSense == Stereo_RightLeft)
      reg = 8-reg; /* Swap stereo */
    switch (reg) {
      /* Centre */
      case 4: channelAmount[i][0] = (ARMword) (0.5*65536);
              channelAmount[i][1] = (ARMword) (0.5*65536);
              break;

      /* Left 100% */
      case 1: channelAmount[i][0] = (ARMword) (1.0*65536);
              channelAmount[i][1] = (ARMword) (0.0*65536);
              break;
      /* Left 83% */
      case 2: channelAmount[i][0] = (ARMword) (0.83*65536);
              channelAmount[i][1] = (ARMword) (0.17*65536);
              break;
      /* Left 67% */
      case 3: channelAmount[i][0] = (ARMword) (0.67*65536);
              channelAmount[i][1] = (ARMword) (0.33*65536);
              break;

      /* Right 100% */
      case 5: channelAmount[i][1] = (ARMword) (1.0*65536);
              channelAmount[i][0] = (ARMword) (0.0*65536);
              break;
      /* Right 83% */
      case 6: channelAmount[i][1] = (ARMword) (0.83*65536);
              channelAmount[i][0] = (ARMword) (0.17*65536);
              break;
      /* Right 67% */
      case 7: channelAmount[i][1] = (ARMword) (0.67*65536);
              channelAmount[i][0] = (ARMword) (0.33*65536);
              break;

      /* Bad setting - just mute it */
      default: channelAmount[i][0] = channelAmount[i][1] = 0;
    }

    /* We have to share each stereo side between the number of channels. */
    channelAmount[i][0] >>= log2numchan;
    channelAmount[i][1] >>= log2numchan;
  }
}

void Sound_SoundFreqUpdated(ARMul_State *state)
{
  /* Do nothing for now */
//  Sound_StereoUpdated(state);
}

typedef void (*Sound_ProcessFunc)(unsigned char *in,SoundData *out,int avail);

static void Sound_Process1Channel(unsigned char *in,SoundData *out,int avail)
{
  /* Process 1-channel audio */
  ARMword left = channelAmount[0][0];
  ARMword right = channelAmount[0][1];
  while(avail--)
  {
    int i;
    for(i=0;i<16;i++)
    {
      signed short int val = soundTable[*in++];
      *out++ = (left * val)>>16;
      *out++ = (right * val)>>16;
    }
  }
}

static void Sound_Process2Channel(unsigned char *in,SoundData *out,int avail)
{
  /* Process 2-channel audio */
  ARMword left0 = channelAmount[0][0];
  ARMword right0 = channelAmount[0][1];
  ARMword left1 = channelAmount[1][0];
  ARMword right1 = channelAmount[1][1];
  while(avail--)
  {
    int i;
    for(i=0;i<8;i++)
    {
      signed short int val0 = soundTable[*in++];
      signed short int val1 = soundTable[*in++];
      ARMword leftmix = (left0 * val0) + (left1 * val1);
      ARMword rightmix = (right0 * val0) + (right1 * val1);
      *out++ = leftmix>>16;
      *out++ = rightmix>>16;
    }
  }
}

static void Sound_Process4Channel(unsigned char *in,SoundData *out,int avail)
{
  /* Process 4-channel audio */
  avail *= 4;
  while(avail--)
  {
    ARMword leftmix=0,rightmix=0;
    int i;
    for(i=0;i<4;i++)
    {
      signed short int val = soundTable[*in++];
      leftmix += channelAmount[i][0] * val;
      rightmix += channelAmount[i][1] * val;
    }
    *out++ = leftmix>>16;
    *out++ = rightmix>>16;
  }
}

static void Sound_Process8Channel(unsigned char *in,SoundData *out,int avail)
{
  /* Process 8-channel audio */
  avail *= 2;
  while(avail--)
  {
    ARMword leftmix=0,rightmix=0;
    int i;
    for(i=0;i<8;i++)
    {
      signed short int val = soundTable[*in++];
      leftmix += channelAmount[i][0] * val;
      rightmix += channelAmount[i][1] * val;
    }
    *out++ = leftmix>>16;
    *out++ = rightmix>>16;
  }
}

static const Sound_ProcessFunc processfuncs[4] =
  {
    Sound_Process1Channel,
    Sound_Process2Channel,
    Sound_Process4Channel,
    Sound_Process8Channel
  };

static void Sound_DMAEvent(ARMul_State *state,CycleCount nowtime)
{
  Sound_UpdateDMARate(state);
  /* How many DMA fetches are possible? */
  int avail = 0;
  if(MEMC.ControlReg & (1 << 11))
    avail = ((MEMC.SendC+16)-MEMC.Sptr)>>4;
  if(avail > Sound_BatchSize)
    avail = Sound_BatchSize;
  /* Work out when to reschedule the event
     This is slightly wrong, since we time it based around how long it takes
     to process this data, but if we finish a buffer we trigger the swap
     immediately instead of at the start of the next event */
  int next = Sound_DMARate*(avail?avail:Sound_BatchSize)+Sound_FudgeRate;
  /* Clamp to a safe minimum value */
  if(next < 100)
    next = 100;
  EventQ_RescheduleHead(state,nowtime+next,Sound_DMAEvent);
  if(avail < 1)
    return;
  /* Process the data */
  (processfuncs[log2numchan])(((unsigned char *) MEMC.PhysRam) + MEMC.Sptr,soundBuffer,avail);
  /* Pass it to the host */
  Sound_HandleData(soundBuffer,(avail*16)>>log2numchan,(VIDC.SoundFreq+2)<<log2numchan);
  /* Update DMA stuff */
  MEMC.Sptr += avail<<4;
  if(MEMC.Sptr > MEMC.SendC)
  {
    /* Have the next buffer addresses been written? */
    if (MEMC.NextSoundBufferValid == 1) {
      /* Yes, so change to the next buffer */
      ARMword swap;

      MEMC.Sptr = MEMC.Sstart;
      MEMC.SstartC = MEMC.Sstart;

      swap = MEMC.SendC;
      MEMC.SendC = MEMC.SendN;
      MEMC.SendN = swap;

      ioc.IRQStatus |= IRQB_SIRQ; /* Take sound interrupt on */
      IO_UpdateNirq(state);

      MEMC.NextSoundBufferValid = 0;
    } else {
      /* Otherwise wrap to the beginning of the buffer */
      MEMC.Sptr = MEMC.SstartC;
    }
  }
}

int Sound_Init(ARMul_State *state)
{
  SoundInitTable();
  Sound_UpdateDMARate(state);
  EventQ_Insert(state,ARMul_Time+Sound_DMARate,Sound_DMAEvent);
  return Sound_InitHost(state);
}
