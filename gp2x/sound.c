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

static unsigned long format = AFMT_S16_LE;
static unsigned long channels = 2;
/*static unsigned long bits = 16;*/
static unsigned long sampleRate = 0;

static unsigned long bufferSize = 0;

/*static unsigned long numberGot = 0;*/

static unsigned long bufferRead = 0;
static unsigned long bufferWrite = 0;

/* This is how many 16 byte blocks to get before leaving
 * sound_poll to return to the emulator */
static unsigned long blocksAtATime = 1;
/* This is how many 16 byte blocks should be collected before
 * calling write(), so this controls the size of the buffer. */
static unsigned long numberOfFills = 100;

/* This is the size of the gap between sound_poll doing something. */
static unsigned long delayTotal = 100;
static unsigned long delayProgress = 0;

static int soundDevice;
static SoundData *buffer = NULL;

static pthread_t thread;
static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void
sound_poll(void)
{
  static unsigned long localBufferWrite = 0;

  delayProgress++;
  if (delayProgress >= delayTotal) {
    int i;

    /*bufferWrite = (numberGot + i) * 16 * 2);*/

    for (i = 0; i < blocksAtATime; i++) {
      /* *Important*, you must respect whether sound dma
       * is enabled or not at all times otherwise sound
       * will come out wrong.
       */
      /*if( SoundDMAFetch(buffer + ((numberGot + i) * 16 * 2)) == 1 ) return; */
      if (SoundDMAFetch(buffer + localBufferWrite) == 1) {
        return;
      }
    }

    localBufferWrite += (blocksAtATime * 16 * 2);
    if (localBufferWrite >= (bufferSize / sizeof(SoundData))) {
      localBufferWrite = 0;
    }

    if (pthread_mutex_trylock(&mut) == 0) {
      bufferWrite = localBufferWrite;
      pthread_mutex_unlock(&mut);
      pthread_cond_broadcast(&cond);
    }
    delayProgress = 0;
  }
}

static void *
sound_writeThread(void *arg)
{
  for (;;) {
    int y;

    pthread_mutex_lock(&mut);
    y = bufferWrite;
    pthread_mutex_unlock(&mut);

    if (bufferRead != y) {
      if (y < bufferRead) {
        y = bufferSize / sizeof(SoundData);
      }
      write(soundDevice, buffer + bufferRead,
            (y - bufferRead) * sizeof(SoundData));
      bufferRead = y;
      if (bufferRead >= (bufferSize / sizeof(SoundData))) {
        bufferRead = 0;
      }
    } else {
      pthread_mutex_lock(&mut);
      pthread_cond_wait(&cond, &mut);
      pthread_mutex_unlock(&mut);
    }
  }

  return NULL;
}

int
sound_init(void)
{
  if ((soundDevice = open("/dev/dsp", O_CREAT | O_WRONLY )) < 0) {
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

  bufferSize = numberOfFills * 16 * 2 * sizeof(SoundData);
  buffer = malloc(bufferSize);
  if (!buffer) {
    fprintf(stderr, "sound_init(): Out of memory\n");
    exit(EXIT_FAILURE);
  }

  pthread_create(&thread, NULL, sound_writeThread, 0);

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
  ioctl(soundDevice, SOUND_PCM_WRITE_RATE, &sampleRate);
  ioctl(soundDevice, SOUND_PCM_READ_RATE, &sampleRate);
  printf("set sample rate to %lu\n", sampleRate);
}
