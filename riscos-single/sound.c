#include <stdio.h>
#include <stdlib.h>

#include <sys/soundcard.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#include "../armdefs.h"
#include "../arch/sound.h"

#include "kernel.h"
#include "swis.h"

/* SharedSound SWI numbers */
#define SharedSound_InstallHandler	0x4B440
#define SharedSound_SampleRate		0x4B446
#define SharedSound_RemoveHandler	0x4B441

int sound_handler_id=0; /* ID of our sound handler (0 if uninstalled) */

#define BUFFER_SAMPLES (16384) /* 8K stereo pairs */

SoundData sound_buffer[BUFFER_SAMPLES];
volatile int sound_buffer_in=BUFFER_SAMPLES; /* Number of samples we've placed in the buffer */
volatile int sound_buffer_out=0; /* Number of samples read out by the IRQ routine */
int sound_rate=0; /* 8.24 fixed point value for how fast we should step through the data */
int sound_buff_mask=BUFFER_SAMPLES-1; /* For benefit of assembler code */

extern void buffer_fill(void); /* Assembler function for performing the buffer fills */
extern void error_handler(void); /* Assembler function attached to ErrorV */

void shutdown_sharedsound(void);

void sigfunc(int sig)
{
	exit(0);
}

int init_sharedsound(void)
{
	/* Try to register sharedsound handler, return nonzero on failure */
	_kernel_swi_regs regs;
	_kernel_oserror *err;
	/* First load SharedSound if it isn't already loaded
	   A *command is the easiest way to do this (usually in your programs !Run file, but this demo doesn't have one) */
	system("RMEnsure SharedSound 0.00 IfThere System:Modules.SSound Then RMLoad System:Modules.SSound");
	/* Now check whether it is loaded, so we can print a more friendly error message than 'SWI xxx not found' */
	regs.r[0] = 18;
	regs.r[1] = (int) "SharedSound";
	if ((err = _kernel_swi(OS_Module,&regs,&regs)))
	{
		printf("Error: SharedSound module not loaded, and not in System:Modules!\n");
		return 1;
	}
	/* Now we can register our handler */
	regs.r[0] = (int) buffer_fill;
	regs.r[1] = 0;
	regs.r[2] = 1;
	regs.r[3] = (int) "ArcEm";
	regs.r[4] = 0;
	if ((err = _kernel_swi(SharedSound_InstallHandler,&regs,&regs)))
	{
		printf("Error: SharedSound_InstallHandler returned error %d, %s\n",err->errnum,err->errmess);
		return 1;
	}
	sound_handler_id = regs.r[0];
	/* Install error handlers */
	atexit(shutdown_sharedsound);
	signal(SIGINT,sigfunc);
	regs.r[0] = 1;
	regs.r[1] = (int) error_handler;
	regs.r[2] = 0;
	_kernel_swi(OS_Claim,&regs,&regs);
	return 0;
}                           

void shutdown_sharedsound(void)
{
	_kernel_swi_regs regs;
	/* Deregister sharedsound handler */
	if(sound_handler_id)
	{
		regs.r[0] = sound_handler_id;
		_kernel_swi(SharedSound_RemoveHandler,&regs,&regs);
		sound_handler_id = 0;
	}
	/* Remove error handler */
	regs.r[0] = 1;
	regs.r[1] = (int) error_handler;
	regs.r[2] = 0;
	_kernel_swi(OS_Release,&regs,&regs); 
}

int Sound_InitHost(ARMul_State *state)
{
  if (init_sharedsound())
  {
    printf("Error: Couldn't register sound handler\n");
    return -1;
  }

  /* We want the right channel first */
  eSound_StereoSense = Stereo_RightLeft;

  /* Use a decent batch size
     We'll receive a max of 8*16*2=256 samples */
  Sound_BatchSize = 8;

  return 0;
}

void Sound_HandleData(const SoundData *buffer,int numSamples,int samplePeriod)
{
  numSamples *= 2;
  static int oldperiod = -1;
  if(samplePeriod != oldperiod)
  {
    oldperiod = samplePeriod;
    int sampleRate = 1024000000/samplePeriod;
    printf("New sample period %d -> rate %d\n",samplePeriod,sampleRate>>10);
    /* Convert to step rate */
    _swix(SharedSound_SampleRate,_INR(0,1)|_OUT(3),sound_handler_id,sampleRate,&sound_rate);
    printf("-> step %08x\n",sound_rate);
  }
  int used = sound_buffer_in-sound_buffer_out;
  int buffree = BUFFER_SAMPLES-used;
  /* Adjust fudge rate
     TODO - Need to be able to instruct the core not to give us so much data that we overflow? */
  if(numSamples > buffree)
  {
    printf("*** sound overflow! %d ***\n",numSamples-buffree);
    numSamples = buffree;
    Sound_FudgeRate+=10;
  }
  else if(!used)
  {
    printf("*** sound underflow! ***\n");
    Sound_FudgeRate-=10;
  }
  else if(used < BUFFER_SAMPLES/4)
  {
    Sound_FudgeRate--;
  }
  else if(buffree < BUFFER_SAMPLES/4)
  {
    Sound_FudgeRate++;
  }
  else if(Sound_FudgeRate)
  {
    /* Bring the fudge value back towards 0 until we go out of the comfort zone */
    Sound_FudgeRate += (Sound_FudgeRate>0?-1:1);
  }
  /* Fill buffer */
  int ofs = sound_buffer_in & sound_buff_mask;
  if(ofs + numSamples > BUFFER_SAMPLES)
  {
    /* Wrap */
    memcpy(sound_buffer+ofs,buffer,(BUFFER_SAMPLES-ofs)*sizeof(SoundData));
    buffer += BUFFER_SAMPLES-ofs;
    sound_buffer_in += BUFFER_SAMPLES-ofs;
    numSamples -= BUFFER_SAMPLES-ofs;
    ofs = 0;
  }
  memcpy(sound_buffer+ofs,buffer,numSamples*sizeof(SoundData));
  sound_buffer_in += numSamples;
}
