/* Amiga sound support routines
 * by Chris Young chris@unsatisfactorysoftware.co.uk
 *
 * This just writes all sound direct to AUDIO: and lets
 * that take care of any buffering.
 */

#include "platform.h"
#include "../armdefs.h"
#include "../arch/sound.h"

BPTR audioh = 0;

static unsigned long sampleRate = 44100;
static unsigned long bufferSize = 64;

/* This is the size of the gap between sound_poll doing something. */
static unsigned long delayTotal = 5; // 100
static unsigned long delayProgress = 0;

static SoundData *buffer = NULL;

void
sound_poll(void)
{
  delayProgress++;
  if (delayProgress >= delayTotal)
	{
	    delayProgress = 0;

	      if (SoundDMAFetch(buffer) == 1)
			{
        		return;
      		}

		IDOS->Write(audioh,buffer,64);

    }
}

int openaudio(void)
{
	STRPTR audiof = NULL;

	if(audiof = IUtility->ASPrintf("AUDIO:BITS/16/C/2/F/%lu/T/SIGNED",sampleRate))
	{
		if(!(audioh = IDOS->Open(audiof,MODE_NEWFILE)))
		{
    		fprintf(stderr, "Could not open audio: device\n");
    		return -1;
		}
		IExec->FreeVec(audiof);
	}
	else
	{
		return -1;
	}
}

int
sound_init(void)
{
//	We only need utility.library in the sound routine, so opening it here.

	if(UtilityBase = IExec->OpenLibrary("utility.library",51))
	{
		IUtility = IExec->GetInterface(UtilityBase,"main",1,NULL);
	}
	else
	{
		return -1;
	}

	if(openaudio() == -1)
		return -1;

  buffer = IExec->AllocVec(bufferSize,MEMF_CLEAR);
  if (!buffer) {
    fprintf(stderr, "sound_init(): Out of memory\n");
    exit(EXIT_FAILURE);
  }

  return 0;
}

/**
 * sound_setSampleRate
 *
 * Set the sample rate of sound, using
 * the period specified in microseconds.
 *
 * @param period period of sample in microseconds
 */
void
sound_setSampleRate(unsigned long period)
{
  /* freq = 1 / (period * 10^-6) */

  if (period != 0) {
    sampleRate = 1000000 / period;
  } else {
    sampleRate = 44100;
  }

  printf("asked to set sample rate to %lu\n", sampleRate);

	IDOS->Close(audioh);	
	openaudio();

  printf("set sample rate to %lu\n", sampleRate);
}

void sound_exit(void)
{
	IExec->FreeVec(buffer);
	IDOS->Close(audioh);

	if(IUtility)
	{
		IExec->DropInterface((struct Interface *)IUtility);
		IExec->CloseLibrary(UtilityBase);
	}
}
