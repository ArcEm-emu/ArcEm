/* Amiga sound support routines
 * by Chris Young chris@unsatisfactorysoftware.co.uk
 *
 * This just writes all sound direct to AUDIO: and lets
 * that take care of any buffering.
 */

#include "platform.h"
#include "../armdefs.h"
#include "../arch/sound.h"
#include "../arch/displaydev.h"

BPTR audioh = 0;

static unsigned long sampleRate = 44100;

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
		fprintf(stderr, "sound_init(): Out of memory\n");
		return -1;
	}
	
	return 0;
}

void Sound_HandleData(const SoundData *buffer,int numSamples,int samplePeriod)
{
	static int oldperiod = -1;
	static unsigned long oldclockin = 0;
	unsigned long clockin = DisplayDev_GetVIDCClockIn(); 

	numSamples *= 2;

	if((samplePeriod != oldperiod) || (clockin != oldclockin))
	{
		oldperiod = samplePeriod;
		oldclockin = clockin;

		if (samplePeriod != 0) {
			sampleRate = clockin / (24*samplePeriod);
		} else {
			sampleRate = 44100;
		}

		printf("asked to set sample rate to %lu\n", sampleRate);

		IDOS->Close(audioh);
		openaudio();

		printf("set sample rate to %lu\n", sampleRate);
	}

	/* TODO - Adjust Sound_FudgeRate to fine-tune how often we receive new data */

	IDOS->Write(audioh,buffer,numSamples*sizeof(SoundData));
}

int
Sound_InitHost(ARMul_State *state)
{
//	We only need utility.library in the sound routine, so opening it here.

	if(UtilityBase = IExec->OpenLibrary("utility.library",51))
	{
		IUtility = IExec->GetInterface(UtilityBase,"main",1,NULL);
	}
	else
	{
		fprintf(stderr, "sound_init(): Could not open utility.library v51\n");
		return -1;
	}

	if(openaudio() == -1)
		return -1;

	/* TODO - Tweak these as necessary */
	eSound_StereoSense = Stereo_LeftRight;

	/* We'll receive a max of 8*16*2=256 samples per call to HandleData */
	Sound_BatchSize = 8;

	return 0;
}

void sound_exit(void)
{
//	IExec->FreeVec(buffer);
	IDOS->Close(audioh);

	if(IUtility)
	{
		IExec->DropInterface((struct Interface *)IUtility);
		IExec->CloseLibrary(UtilityBase);
	}
}
