/*
  riscos-single/sound.c

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Basic SharedSound sound driver for ArcEm, based in part on my SharedSound
  tutorial from http://www.iconbar.com/forums/viewthread.php?threadid=10778

*/

#if defined(SOUND_SUPPORT)

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "../armdefs.h"
#include "../arch/sound.h"
#include "../arch/archio.h"
#include "../arch/dbugsys.h"
#include "../arch/displaydev.h"

#include "kernel.h"
#include "swis.h"

/* SharedSound SWI numbers */
#define SharedSound_InstallHandler	0x4B440
#define SharedSound_SampleRate		0x4B446
#define SharedSound_RemoveHandler	0x4B441

int sound_handler_id=0; /* ID of our sound handler (0 if uninstalled) */

#define BUFFER_SAMPLES (32768) /* 16K stereo pairs */

SoundData sound_buffer[BUFFER_SAMPLES];
volatile int sound_buffer_in=BUFFER_SAMPLES; /* Number of samples we've placed in the buffer */
volatile int sound_buffer_out=0; /* Number of samples read out by the IRQ routine */
int sound_buff_mask=BUFFER_SAMPLES-1; /* For benefit of assembler code */
int sound_rate = 1<<24; /* Fixed output rate! */
static int buffer_threshold; /* Threshold value used to control desired buffer level; chosen based around the output sample rate & buffer_seconds */
static float buffer_seconds = 0.2f; /* How much audio we want to try and keep buffered */ 

extern void buffer_fill(void); /* Assembler function for performing the buffer fills */
extern void error_handler(void); /* Assembler function attached to ErrorV */

static void shutdown_sharedsound(void);

#ifdef __TARGET_UNIXLIB__
extern void __write_backtrace(int signo);
static void sigfunc(int sig)
{
	shutdown_sharedsound();
#if 0
	/* Dump some emulator state */
	ARMul_State *state = &statestr;
	dbug_vidc("r0 = %08x  r4 = %08x  r8  = %08x  r12 = %08x\n"
	          "r1 = %08x  r5 = %08x  r9  = %08x  sp  = %08x\n"
	          "r2 = %08x  r6 = %08x  r10 = %08x  lr  = %08x\n"
	          "r3 = %08x  r7 = %08x  r11 = %08x  pc  = %08x\n"
	          "\n",
	  state->Reg[0], state->Reg[4], state->Reg[8], state->Reg[12],
	  state->Reg[1], state->Reg[5], state->Reg[9], state->Reg[13],
	  state->Reg[2], state->Reg[6], state->Reg[10], state->Reg[14],
	  state->Reg[3], state->Reg[7], state->Reg[11], state->Reg[15]);
	int i;
	for(i=0;i<4;i++)
	  dbug_vidc("Timer%d Count %08x Latch %08x\n",i,ioc.TimerCount[i],ioc.TimerInputLatch[i]);
	FILE *f = fopen("$.dump","wb");
	if(f)
	{
		for(i=0;i<1024*1024;i+=4)
		{
			ARMword word = ARMul_LoadWordS(state,i);
			if(state->abortSig)
				break;
			fwrite(&word,4,1,f);
		}
		fclose(f);
	}
#endif

	/* Now dump a backtrace of ourself */
	__write_backtrace(sig);
	exit(0);
}
#endif

static int init_sharedsound(void)
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
		warn_vidc("Warning: SharedSound module not loaded, and not in System:Modules!\n");
		return 0;
	}
	/* Now we can register our handler */
	regs.r[0] = (int) buffer_fill;
	regs.r[1] = 0;
	regs.r[2] = 1;
	regs.r[3] = (int) "ArcEm";
	regs.r[4] = 0;
	if ((err = _kernel_swi(SharedSound_InstallHandler,&regs,&regs)))
	{
		warn_vidc("Warning: SharedSound_InstallHandler returned error %d, %s\n",err->errnum,err->errmess);
		return 0;
	}
	sound_handler_id = regs.r[0];
	/* Install error handlers so we don't crash on exit */
	atexit(shutdown_sharedsound);
#ifdef __TARGET_UNIXLIB__
	/* With UnixLib we need to use signal handlers to catch any errors.
	   UnixLib doesn't make it very easy to get user code to run when
	   something bad happens :( */
	signal(SIGHUP,sigfunc);
	signal(SIGINT,sigfunc);
	signal(SIGQUIT,sigfunc);
	signal(SIGILL,sigfunc);
	signal(SIGABRT,sigfunc);
	signal(SIGTRAP,sigfunc);
	signal(SIGEMT,sigfunc);
	signal(SIGFPE,sigfunc);
	signal(SIGKILL,sigfunc);
	signal(SIGBUS,sigfunc);
	signal(SIGSEGV,sigfunc);
	signal(SIGSYS,sigfunc);
	signal(SIGPIPE,sigfunc);
	signal(SIGALRM,sigfunc);
	signal(SIGTERM,sigfunc);
	signal(SIGSTOP,sigfunc);
	signal(SIGTSTP,sigfunc);
	signal(SIGTTIN,sigfunc);
	signal(SIGTTOU,sigfunc);
	signal(SIGOSERROR,sigfunc);
#else
	/* With SCL a simple error handler seems to do the trick */
	regs.r[0] = 1;
	regs.r[1] = (int) error_handler;
	regs.r[2] = 0;
	_kernel_swi(OS_Claim,&regs,&regs);
#endif
	/* Get our sample rate */
	_swix(SharedSound_SampleRate,_INR(0,1)|_OUT(1),sound_handler_id,0,&Sound_HostRate);
	/* Calculate the buffer threshold value
	   This is the low threshold value, so we divide by 512 to get the desired level, then by two again to get the low level */
	buffer_threshold = ((int)(buffer_seconds*((float)Sound_HostRate)))>>10;
	/* Low thresholds may cause issues */
	if(buffer_threshold<Sound_BatchSize*4)
		buffer_threshold = Sound_BatchSize*4;
	/* Big thresholds will cause problems too! */
	if(buffer_threshold*4 > BUFFER_SAMPLES)
		buffer_threshold = BUFFER_SAMPLES>>2;
	warn_vidc("Host audio rate %dHz, using buffer threshold %d\n",Sound_HostRate>>10,buffer_threshold);
	return 0;
}                           

static void shutdown_sharedsound(void)
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
  /* We want the right channel first */
  eSound_StereoSense = Stereo_RightLeft;

  /* Use a decent batch size */
  Sound_BatchSize = 256;

  /* Default 20833Hz sample rate if no SharedSound? */
  Sound_HostRate = (1000000<<10)/48;

#ifndef PROFILE_ENABLED
  if (init_sharedsound())
  {
    warn_vidc("Error: Couldn't register sound handler\n");
    return -1;
  }
#endif

  return 0;
}

SoundData *Sound_GetHostBuffer(int32_t *destavail)
{
  int used, ofs, buffree;
  /* Work out how much space is available until next wrap point, or we start overwriting data */
  if(!sound_handler_id)
  {
    *destavail = BUFFER_SAMPLES>>1;
    return sound_buffer;
  }
  used = sound_buffer_in-sound_buffer_out;
  ofs = sound_buffer_in & sound_buff_mask;
  buffree = BUFFER_SAMPLES-MAX(ofs,used);
  *destavail = buffree>>1;
  return sound_buffer + ofs;
}

void Sound_HostBuffered(SoundData *buffer,int32_t numSamples)
{
  int used, buffree;

  if(!sound_handler_id)
    return;

  used = sound_buffer_in-sound_buffer_out;
  buffree = BUFFER_SAMPLES-used;

  numSamples <<= 1;

  sound_buffer_in += numSamples;

  if(buffree == numSamples)
  {
    warn_vidc("*** sound overflow! ***\n");
    if(Sound_FudgeRate < -10)
      Sound_FudgeRate = Sound_FudgeRate/2;
    else
      Sound_FudgeRate+=10;
  }
  else if(!used)
  {
    warn_vidc("*** sound underflow! ***\n");
    if(Sound_FudgeRate > 10)
      Sound_FudgeRate = Sound_FudgeRate/2;
    else
      Sound_FudgeRate-=10;
  }
  else if(used < buffer_threshold)
  {
    Sound_FudgeRate-=3;
  }
  else if(used < buffer_threshold*3)
  {
    Sound_FudgeRate+=3;
  }
  else if(Sound_FudgeRate)
  {
    /* Bring the fudge value back towards 0 until we go out of the comfort zone */
    Sound_FudgeRate += (Sound_FudgeRate>0?-1:1);
  }
}

#endif
