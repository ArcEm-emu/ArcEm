#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "../armdefs.h"
#include "../arch/sound.h"
#include "../arch/displaydev.h"
#include "../armemu.h"

#define BUFFER_SAMPLES (16384) /* 8K stereo pairs */

SoundData sound_buffer[BUFFER_SAMPLES];
volatile int32_t sound_buffer_in=BUFFER_SAMPLES; /* Number of samples we've placed in the buffer */
volatile int32_t sound_buffer_out=0; /* Number of samples read out by the sound thread */
static const int32_t sound_buff_mask=BUFFER_SAMPLES-1;

#if SDL_VERSION_ATLEAST(2, 0, 0)
static SDL_AudioDeviceID device_id;

static void Sound_Lock(void)
{
  SDL_LockAudioDevice(device_id);
}

static void Sound_Unlock(void)
{
  SDL_UnlockAudioDevice(device_id);
}

static void Sound_Pause(int pause_on)
{
  SDL_PauseAudioDevice(device_id, pause_on);
}
#else
static void Sound_Lock(void)
{
  SDL_LockAudio();
}

static void Sound_Unlock(void)
{
  SDL_UnlockAudio();
}

static void Sound_Pause(int pause_on)
{
  SDL_PauseAudio(pause_on);
}
#endif

SoundData *Sound_GetHostBuffer(int32_t *destavail)
{
  /* Work out how much space is available until next wrap point, or we start overwriting data */
  Sound_Lock();
  int32_t local_buffer_in = sound_buffer_in;
  int32_t used = local_buffer_in-sound_buffer_out;
  Sound_Unlock();
  int32_t ofs = local_buffer_in & sound_buff_mask;
  int32_t buffree = BUFFER_SAMPLES-MAX(ofs,used);
  *destavail = buffree>>1;
  return sound_buffer + ofs;
}

void Sound_HostBuffered(SoundData *buffer,int32_t numSamples)
{
  numSamples <<= 1;
  Sound_Lock();
  int32_t local_buffer_in = sound_buffer_in;
  int32_t used = local_buffer_in-sound_buffer_out;
  Sound_Unlock();
  int32_t ofs = local_buffer_in & sound_buff_mask;
  int32_t buffree = BUFFER_SAMPLES-MAX(ofs,used);

  local_buffer_in += numSamples;

  if(buffree == numSamples)
  {
    fprintf(stderr,"*** sound overflow! ***\n");
    if(Sound_FudgeRate < -10)
      Sound_FudgeRate = Sound_FudgeRate/2;
    else
      Sound_FudgeRate+=10;
  }
  else if(!used)
  {
    fprintf(stderr,"*** sound underflow! ***\n");
    if(Sound_FudgeRate > 10)
      Sound_FudgeRate = Sound_FudgeRate/2;
    else
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

  Sound_Lock();
  sound_buffer_in = local_buffer_in;
  Sound_Unlock();
}

static void Sound_Callback(void *userdata, Uint8 *stream, int len)
{
  static int32_t local_buffer_out = 0;
  while (len) {
    int32_t avail;

    Sound_Lock();
    sound_buffer_out = local_buffer_out;
    avail = sound_buffer_in-local_buffer_out;
    Sound_Unlock();

    if (avail) {
      int32_t ofs = local_buffer_out & sound_buff_mask;

      if(ofs + avail > BUFFER_SAMPLES) {
        /* We're about to wrap */
        avail = BUFFER_SAMPLES-ofs;
      }

      if (avail * sizeof(SoundData) > len) {
        avail = len / sizeof(SoundData);
      }

      memcpy(stream, sound_buffer + ofs,
            avail * sizeof(SoundData));

      local_buffer_out += avail;

      stream += avail * sizeof(SoundData);
      len -= avail * sizeof(SoundData);
    } else {
      memset(stream, 0, len);
      return;
    }
  }
}

int
Sound_InitHost(ARMul_State *state)
{
  SDL_AudioSpec desired, obtained;

  desired.freq = 44100;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 4096;
  desired.callback = Sound_Callback;
  desired.userdata = NULL;

#if SDL_VERSION_ATLEAST(2, 0, 0)
  device_id = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained,
                                  SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  if (device_id == 0)
#else
  if (SDL_OpenAudio(&desired, &obtained) < 0)
#endif
  {
    fprintf(stderr, "Could not open audio device: %s\n", SDL_GetError());
    return -1;
  }

  eSound_StereoSense = Stereo_LeftRight;

  /* Use a decent batch size */
  Sound_BatchSize = 256;

  Sound_HostRate = obtained.freq<<10;

  Sound_Pause(0);

  return 0;
}
