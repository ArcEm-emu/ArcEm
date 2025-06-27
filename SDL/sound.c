#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "../armdefs.h"
#include "../arch/sound.h"
#include "../arch/displaydev.h"
#include "../armemu.h"

/* #define SOUND_LOGGING */

/* #define SOUND_VERBOSE */

#define BUFFER_SAMPLES (32768) /* 16K stereo pairs */

SoundData sound_buffer[BUFFER_SAMPLES];
volatile int32_t sound_buffer_in=BUFFER_SAMPLES/2; /* Number of samples we've placed in the buffer */
volatile int32_t sound_buffer_out=0; /* Number of samples read out by the sound thread */
volatile int32_t sound_underflows=0;
volatile int32_t sound_buffer_hostout=0; /* Number of samples requested by the sound thread */
static const int32_t sound_buff_mask=BUFFER_SAMPLES-1;
static float sound_inv_hostrate; /* 1/Sound_HostRate */
static int32_t buffer_threshold; /* Desired buffer level; chosen based around the output sample rate & buffer_seconds */
static const float buffer_seconds = 0.1f; /* How much audio we want to try and keep buffered */

#ifdef SOUND_LOGGING

#if SDL_VERSION_ATLEAST(2, 0, 0)
#define GETCOUNTER SDL_GetPerformanceCounter()
#define GETCOUNTERRATE SDL_GetPerformanceFrequency()
#else
/* For simplicity, fall back to the 1ms timer for SDL 1.
 * Could be replaced with something better (C11 timespec_get?
 * POSIX clock_gettime?) on a per-platform basis if you're
 * after high-precision timing on SDL 1
 */
#define GETCOUNTER ((uint64_t) SDL_GetTicks())
#define GETCOUNTERRATE ((uint64_t) 1000)
#endif

static FILE *logfile;
static Uint64 logt0;
static void LOG_EVENT(const char *event, int32_t delta)
{
  if (logfile)
  {
    fprintf(logfile,"%s,%llu,%d,%d\n",event,GETCOUNTER-logt0,sound_buffer_in-sound_buffer_out,delta);
  }
}
#else
#define LOG_EVENT(X,Y) do { } while (0)
#endif

/* Discrete implementation of a PID controller for adjusting the FudgeRate
 * https://en.wikipedia.org/wiki/Proportional-integral-derivative_controller#Pseudocode */
typedef struct {
  float A0,A1,A2; /* Precomputed constants */
  int32_t error[3]; /* History of error values */
} PIDController;

#define PID_PERIOD (0.25f) /* Seconds */

static PIDController pid_init(float Kp, float Ki, float Kd, float dt)
{
  PIDController p = {0};
  float scale = ((float) (1<<24)) * sound_inv_hostrate; /* Scale all of the PID coefficients so that errors which are specified in samples get converted to adjustments in (1<<24)'ths of a second */
  p.A0 = (Kp + Ki*dt + Kd/dt) * scale;
  p.A1 = (-Kp - 2*Kd/dt) * scale;
  p.A2 = (Kd/dt) * scale;
  return p;
}

static PIDController pid_init_zn(float Ku, float Tu, float dt)
{
  float Kp = 0.6f*Ku;
  float Ki = 1.2f*Ku/Tu;
  float Kd = 0.075f*Ku*Tu;
#ifdef SOUND_VERBOSE
  fprintf(stderr, "Ziegler-Nichols params Ku %f Tu %f dt %f -> Kp %f Ki %f Kd %f\n", Ku, Tu, dt, Kp, Ki, Kd);
#endif
  return pid_init(Kp, Ki, Kd, dt);
}

static int32_t pid_step(PIDController *p)
{
  float adjustment = p->A0*p->error[0] + p->A1*p->error[1] + p->A2*p->error[2];
  /* Clamp to within safe limits */
  if (adjustment > (2<<24))
  {
    adjustment = 2<<24;
  }
  else if (adjustment < -(2<<24))
  {
    adjustment = -(2<<24);
  }
  p->error[2] = p->error[1];
  p->error[1] = p->error[0];
  p->error[0] = 0;
  return (int32_t) adjustment;
}

static PIDController sound_pid;

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

static void adjust_fudgerate(int32_t used, int32_t out)
{
  static float count;
  static int32_t error_count,old_out;
  int32_t error = used - buffer_threshold;
  error = error >> 1; /* Re-scale to 16 bit sample pairs, so all our rate tracking will work correctly */
  sound_pid.error[0] += error;
#ifdef SOUND_VERBOSE
  static int32_t emin,emax;
  if (error < emin) emin = error;
  if (error > emax) emax = error;
#endif
  /* Use number of host samples as our clock source, since that's likely to be a more stable clock than the one used to generate the samples
   * The timing of PID adjustments will be a bit rough (we behave as if there's always PID_PERIOD time between each adjustment), but it still seems to work OK */
  count += sound_inv_hostrate * ((out-old_out) >> 1);
  old_out = out;
  error_count++;
  if (count < PID_PERIOD)
  {
    return;
  }
  /* Preserve remainder as long as it's not too large */
  count -= PID_PERIOD;
  if (count >= PID_PERIOD)
  {
    count = 0;
  }
  sound_pid.error[0] /= error_count;
  error_count = 0;
  int32_t adjust = pid_step(&sound_pid);
#ifdef SOUND_VERBOSE
  fprintf(stderr, "level %f avg error %f (%f,%f,%f) fudge %f adjust %f remainder %f\n", sound_inv_hostrate*0.5f*used, sound_inv_hostrate*sound_pid.error[1], sound_inv_hostrate*emin, sound_inv_hostrate*emax, sound_inv_hostrate*(emax-emin), ((float)Sound_FudgeRate)/(1<<24), ((float)adjust)/(1<<24), count);
  emin = BUFFER_SAMPLES;
  emax = -BUFFER_SAMPLES;
#endif
  adjust += Sound_FudgeRate;
  /* Limit to within range [0.5,2.0] */
  if (adjust < 1<<23)
  {
    adjust = 1<<23;
  }
  else if (adjust > 2<<24)
  {
    adjust = 2<<24;
  }
  Sound_FudgeRate = adjust;
}

void Sound_HostBuffered(SoundData *buffer,int32_t numSamples)
{
  numSamples <<= 1;
  Sound_Lock();
  int32_t local_buffer_in = sound_buffer_in;
  int32_t used = local_buffer_in-sound_buffer_out;
  int32_t out = sound_buffer_hostout;
  int32_t underflows = sound_underflows;
  sound_underflows = 0;
  LOG_EVENT("Sound_HostBuffered",numSamples);
  Sound_Unlock();

  local_buffer_in += numSamples;

  if (underflows)
  {
    fprintf(stderr, "*** sound underflow x%d! ***\n", underflows);
  }

  adjust_fudgerate(used, out);

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
    sound_buffer_hostout += len/sizeof(SoundData);
    sound_buffer_out = local_buffer_out;
    LOG_EVENT("Sound_Callback",-(len/sizeof(SoundData)));
    avail = sound_buffer_in-local_buffer_out;
    if (avail * (int32_t)sizeof(SoundData) < len)
      sound_underflows++;
    Sound_Unlock();

    if (avail) {
      int32_t ofs = local_buffer_out & sound_buff_mask;

      if(ofs + avail > BUFFER_SAMPLES) {
        /* We're about to wrap */
        avail = BUFFER_SAMPLES-ofs;
      }

      if (avail * (int32_t)sizeof(SoundData) > len) {
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
  desired.samples = 512;
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

#ifdef SOUND_LOGGING
  logfile = fopen("soundlog.csv","w");
  if (logfile)
  {
    fprintf(logfile,"event,time,used,delta\n");
  }
  fprintf(stderr, "Timer frequency %llu\n", GETCOUNTERRATE);
  logt0 = GETCOUNTER;
#endif

  sound_inv_hostrate = 1.0f / obtained.freq;

  /* sound_pid = pid_init(8.0f,0,0,PID_PERIOD); */
  sound_pid = pid_init_zn(2,1.0f,PID_PERIOD); /* 8 is a more correct Ku value, but use 2 to make the corrections less aggressive */

  eSound_StereoSense = Stereo_LeftRight;

  /* Use a decent batch size */
  Sound_BatchSize = 256;

  Sound_HostRate = obtained.freq<<10;

  /* Calculate the desired buffer level */
  buffer_threshold = ((int)(buffer_seconds*obtained.freq))<<1;
  /* Low thresholds may cause issues */
  if (buffer_threshold < Sound_BatchSize*4)
    buffer_threshold = Sound_BatchSize*4;
  if (buffer_threshold < obtained.samples*2*4)
    buffer_threshold = obtained.samples*2*4;
  /* Big thresholds will cause problems too! */
  if (buffer_threshold+Sound_BatchSize*4 > BUFFER_SAMPLES)
    buffer_threshold = BUFFER_SAMPLES-Sound_BatchSize*4;

  sound_buffer_in = buffer_threshold;

  fprintf(stderr, "SDL_OpenAudio got freq %d samples %d desired level %d (%fs)\n", obtained.freq, obtained.samples, buffer_threshold, sound_inv_hostrate*0.5f*buffer_threshold);

  Sound_Pause(0);

  return 0;
}

void
Sound_ShutdownHost(ARMul_State *state)
{
#ifdef SOUND_LOGGING
  if (logfile)
  {
    fclose(logfile);
    logfile = NULL;
  }
#endif
#if SDL_VERSION_ATLEAST(2, 0, 0)
  SDL_CloseAudioDevice(device_id);
#else
  SDL_CloseAudio();
#endif
}
