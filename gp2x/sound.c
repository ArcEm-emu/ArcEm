#include <stdio.h>
#include <stdlib.h>

#include <linux/soundcard.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>

#include "../armdefs.h"
#include "../arch/sound.h"
#include "../arch/displaydev.h"
#include "../armemu.h"

static uint32_t format = AFMT_S16_LE;
static uint32_t channels = 2;
static uint32_t sampleRate = 44100;

static int soundDevice;

/* Threading currently doesn't work very well - no priority control is in place, so the sound thread hardly ever gets any CPU time */
//#define SOUND_THREAD

#ifdef SOUND_THREAD
static pthread_t thread;
static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

#define BUFFER_SAMPLES (16384) /* 8K stereo pairs */

SoundData sound_buffer[BUFFER_SAMPLES];
volatile int32_t sound_buffer_in=BUFFER_SAMPLES; /* Number of samples we've placed in the buffer */
volatile int32_t sound_buffer_out=0; /* Number of samples read out by the sound thread */
static const int32_t sound_buff_mask=BUFFER_SAMPLES-1;
#else
SoundData sound_buffer[256*2]; /* Must be >= 2*Sound_BatchSize! */
#endif

SoundData *Sound_GetHostBuffer(int32_t *destavail)
{
#ifdef SOUND_THREAD
  /* Work out how much space is available until next wrap point, or we start overwriting data */
  pthread_mutex_lock(&mut);
  int32_t local_buffer_in = sound_buffer_in;
  int32_t used = local_buffer_in-sound_buffer_out;
  pthread_mutex_unlock(&mut);
  int32_t ofs = local_buffer_in & sound_buff_mask;
  int32_t buffree = BUFFER_SAMPLES-MAX(ofs,used);
  *destavail = buffree>>1;
  return sound_buffer + ofs;
#else
  /* Just assume we always have enough space for the max batch size */
  *destavail = sizeof(sound_buffer)/(sizeof(SoundData)*2);
  return sound_buffer;
#endif
}

void Sound_HostBuffered(SoundData *buffer,int32_t numSamples)
{
  numSamples <<= 1;
#ifdef SOUND_THREAD
  pthread_mutex_lock(&mut);
  int32_t local_buffer_in = sound_buffer_in;
  int32_t used = local_buffer_in-sound_buffer_out;
  pthread_mutex_unlock(&mut);
  int32_t ofs = local_buffer_in & sound_buff_mask;

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

  pthread_mutex_lock(&mut);
  sound_buffer_in = local_buffer_in;
  pthread_mutex_unlock(&mut);
  pthread_cond_broadcast(&cond);

  pthread_yield();
#else
  audio_buf_info buf;
  if (ioctl(soundDevice, SOUND_PCM_GETOSPACE, &buf) != -1) {
    /* Adjust fudge rate based around how much buffer space is available
       We aim for the buffer to be somewhere between 1/4 and 3/4 full, but don't
       explicitly set the buffer size, so we're at the mercy of the sound system
       in terms of how much lag there'll be */
    int32_t bufsize = buf.fragsize*buf.fragstotal;
    int32_t buffree = buf.bytes/sizeof(SoundData);
    int32_t used = (bufsize-buf.bytes)/sizeof(SoundData);
    int32_t stepsize = Sound_DMARate>>2;
    bufsize /= sizeof(SoundData);
    if(numSamples > buffree)
    {
      fprintf(stderr,"*** sound overflow! %d %d %d %d ***\n",numSamples-buffree,ARMul_EmuRate,Sound_FudgeRate,Sound_DMARate);
      numSamples = buffree; /* We could block until space is available, but I'm woried we'd get stuck blocking forever because the FudgeRate increase wouldn't compensate for the ARMul cycles lost due to blocking */
      if(Sound_FudgeRate < -stepsize)
        Sound_FudgeRate = Sound_FudgeRate/2;
      else
        Sound_FudgeRate+=stepsize;
    }
    else if(!used)
    {
      fprintf(stderr,"*** sound underflow! %d %d %d ***\n",ARMul_EmuRate,Sound_FudgeRate,Sound_DMARate);
      if(Sound_FudgeRate > stepsize)
        Sound_FudgeRate = Sound_FudgeRate/2;
      else
        Sound_FudgeRate-=stepsize;
    }
    else if(used < bufsize/4)
    {
      Sound_FudgeRate-=stepsize>>4;
    }
    else if(buffree < bufsize/4)
    {
      Sound_FudgeRate+=stepsize>>4;
    }
    else if(Sound_FudgeRate)
    {
      /* Bring the fudge value back towards 0 until we go out of the comfort zone */
      Sound_FudgeRate += (Sound_FudgeRate>0?-1:1);
    }
  }
  
  write(soundDevice,buffer,numSamples*sizeof(SoundData));
#endif
}

#ifdef SOUND_THREAD
static void *
sound_writeThread(void *arg)
{
  int32_t local_buffer_out = sound_buffer_out;
  for (;;) {
    int32_t avail;

    pthread_mutex_lock(&mut);
    sound_buffer_out = local_buffer_out;
    avail = sound_buffer_in-local_buffer_out;
    pthread_mutex_unlock(&mut);

    printf("%d\n",avail);
    if (avail) {
      int32_t ofs = local_buffer_out & sound_buff_mask;

      if(ofs + avail > BUFFER_SAMPLES) {
        /* We're about to wrap */
        avail = BUFFER_SAMPLES-ofs;
      }

      write(soundDevice, sound_buffer + ofs,
            avail * sizeof(SoundData));

      local_buffer_out += avail;
    } else {
      pthread_mutex_lock(&mut);
      pthread_cond_wait(&cond, &mut);
      pthread_mutex_unlock(&mut);
    }
  }

  return NULL;
}
#endif

int
Sound_InitHost(ARMul_State *state)
{
  if ((soundDevice = open("/dev/dsp", O_WRONLY )) < 0) {
    fprintf(stderr, "Could not open audio device /dev/dsp\n");
    return -1;
  }

  if (ioctl(soundDevice, SOUND_PCM_RESET, 0) == -1) {
    fprintf(stderr, "Could not reset PCM\n");
    return -1;
  }

  if (ioctl(soundDevice, SOUND_PCM_SYNC, 0) == -1) {
    fprintf(stderr, "Could not sync PCM\n");
    return -1;
  }

  if (ioctl(soundDevice, SOUND_PCM_SETFMT, &format) == -1) {
    fprintf(stderr, "Could not set PCM format\n");
    return -1;
  }

  if (ioctl(soundDevice, SOUND_PCM_WRITE_CHANNELS, &channels) == -1) {
    fprintf(stderr,"Could not set to 2 channel stereo\n");
    return -1;
  }

  if (ioctl(soundDevice, SOUND_PCM_WRITE_RATE, &sampleRate) == -1) {
    fprintf(stderr, "Could not set sample rate\n");
    return -1;
  }

  if (ioctl(soundDevice, SOUND_PCM_READ_RATE, &sampleRate) == -1) {
    fprintf(stderr, "Could not read sample rate\n");
    return -1;
  }

  /* Check that GETOSPACE is supported */
  audio_buf_info buf;
  if (ioctl(soundDevice, SOUND_PCM_GETOSPACE, &buf) == -1) {
    fprintf(stderr,"Could not read output space\n");
    return -1;
  }
  fprintf(stderr,"Sound buffer params: frags %d total %d size %d bytes %d\n",buf.fragments,buf.fragstotal,buf.fragsize,buf.bytes);

  eSound_StereoSense = Stereo_LeftRight;

  /* Use a decent batch size */
  Sound_BatchSize = 256;

  Sound_HostRate = sampleRate<<10;

#ifdef SOUND_THREAD
  pthread_create(&thread, NULL, sound_writeThread, 0);
#endif

  return 0;
}
