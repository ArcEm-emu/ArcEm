//
//  sound.c
//  ArcEm
//
//  Created by C.W. Betts on 6/6/18.
//

#include <CoreAudio/CoreAudio.h>
#include "../armdefs.h"
#include "../arch/sound.h"
#include "../arch/displaydev.h"

static const unsigned long sampleRate = 44100;
static SoundData sound_buffer[256*2]; /* Must be >= 2*Sound_BatchSize! */

//TODO: implement using a macOS framework like CoreAudio

int openaudio(void)
{
	return -1;
}

int
Sound_InitHost(ARMul_State *state)
{
	return -1;
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
	
	/* TODO: - Adjust Sound_FudgeRate to fine-tune how often we receive new data */
	
	//Write(audioh,buffer,numSamples*sizeof(SoundData));
}

void sound_exit(void)
{
	
}
