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

SoundData sound_buffer[256*2]; /* Must be >= 2*Sound_BatchSize! */

int openaudio(void)
{
	char audiof[256];

	sprintf(audiof, "AUDIO:BITS/16/C/2/F/%lu/T/SIGNED\0", sampleRate);

	if(!(audioh = Open(audiof,MODE_NEWFILE)))
	{
		fprintf(stderr, "Could not open audio: device\n");
		return -1;
	}

	return 0;
}

SoundData *Sound_GetHostBuffer(int32_t *destavail)
{
	/* Just assume we always have enough space for the max batch size */
	*destavail = sizeof(sound_buffer)/(sizeof(SoundData)*2);
	return sound_buffer;
}

void Sound_HostBuffered(SoundData *buffer,int32_t numSamples)
{
	numSamples *= 2;

	/* TODO - Adjust Sound_FudgeRate to fine-tune how often we receive new data */

	Write(audioh,buffer,numSamples*sizeof(SoundData));
}

int
Sound_InitHost(ARMul_State *state)
{
	if(openaudio() == -1)
		return -1;

	/* TODO - Tweak these as necessary */
	eSound_StereoSense = Stereo_LeftRight;

	Sound_BatchSize = 256;

	Sound_HostRate = sampleRate<<10;

	return 0;
}

void sound_exit(void)
{
//	IExec->FreeVec(buffer);
	Close(audioh);
}
