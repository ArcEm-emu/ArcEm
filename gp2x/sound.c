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

static unsigned long format = AFMT_S16_LE;
static unsigned long channels = 2;
static unsigned long sampleRate = 44100;

static int soundDevice;

/* Threadng currently doesn't work very well - no priority control is in place, so the sound thread hardly ever gets any CPU time */
//#define SOUND_THREAD

#ifdef SOUND_THREAD
static pthread_t thread;
static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

#define BUFFER_SAMPLES (16384) /* 8K stereo pairs */

SoundData sound_buffer[BUFFER_SAMPLES];
volatile int sound_buffer_in=BUFFER_SAMPLES; /* Number of samples we've placed in the buffer */
volatile int sound_buffer_out=0; /* Number of samples read out by the sound thread */
static const int sound_buff_mask=BUFFER_SAMPLES-1;
#endif

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
    ioctl(soundDevice, SOUND_PCM_WRITE_RATE, &sampleRate);
    ioctl(soundDevice, SOUND_PCM_READ_RATE, &sampleRate);
    printf("set sample rate to %lu\n", sampleRate);
    /* TODO - Cope properly with situations where we can't get the desired rate */
  }

#ifdef SOUND_THREAD
  pthread_mutex_lock(&mut);
  int local_buffer_in = sound_buffer_in;
  int used = local_buffer_in-sound_buffer_out;
  pthread_mutex_unlock(&mut);
  
  int buffree = BUFFER_SAMPLES-used;
  /* Adjust fudge rate
     TODO - Need to be able to instruct the core not to give us so much data that we overflow */
  if(numSamples > buffree)
  {
    fprintf(stderr,"*** sound overflow! %d ***\n",numSamples-buffree);
    numSamples = buffree;
    Sound_FudgeRate+=10;
  }
  else if(!used)
  {
    fprintf(stderr,"*** sound underflow! ***\n");
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
  int ofs = local_buffer_in & sound_buff_mask;
  if(ofs + numSamples > BUFFER_SAMPLES)
  {
    /* Wrap */
    memcpy(sound_buffer+ofs,buffer,(BUFFER_SAMPLES-ofs)*sizeof(SoundData));
    buffer += BUFFER_SAMPLES-ofs;
    local_buffer_in += BUFFER_SAMPLES-ofs;
    numSamples -= BUFFER_SAMPLES-ofs;
    ofs = 0;
  }
  memcpy(sound_buffer+ofs,buffer,numSamples*sizeof(SoundData));
  local_buffer_in += numSamples;

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
    int bufsize = buf.fragsize*buf.fragstotal;
    int buffree = buf.bytes/sizeof(SoundData);
    int used = (bufsize-buf.bytes)/sizeof(SoundData);
    bufsize /= sizeof(SoundData);
    if(numSamples > buffree)
    {
      fprintf(stderr,"*** sound overflow! %d ***\n",numSamples-buffree);
      numSamples = buffree; /* We could block until space is available, but I'm woried we'd get stuck blocking forever because the FudgeRate increase wouldn't compensate for the ARMul cycles lost due to blocking */
      Sound_FudgeRate+=10;
    }
    else if(!used)
    {
      fprintf(stderr,"*** sound underflow! ***\n");
      Sound_FudgeRate-=10;
    }
    else if(used < bufsize/4)
    {
      Sound_FudgeRate--;
    }
    else if(buffree < bufsize/4)
    {
      Sound_FudgeRate++;
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
  int local_buffer_out = sound_buffer_out;
  for (;;) {
    int avail;

    pthread_mutex_lock(&mut);
    sound_buffer_out = local_buffer_out;
    avail = sound_buffer_in-local_buffer_out;
    pthread_mutex_unlock(&mut);

    printf("%d\n",avail);
    if (avail) {
      int ofs = local_buffer_out & sound_buff_mask;

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
    fprintf(stderr, "Could not set initial sample rate\n");
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

  /* Use a decent batch size
     We'll receive a max of 8*16*2=256 samples */
  Sound_BatchSize = 8;

#ifdef SOUND_THREAD
  pthread_create(&thread, NULL, sound_writeThread, 0);
#endif

  return 0;
}
