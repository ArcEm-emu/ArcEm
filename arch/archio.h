#ifndef ARCHIO_HEADER
#define ARCHIO_HEADER
/* (c) David Alan Gilbert 1995-1998 - see Readme file for copying info */

#include <signal.h>
#include "../armopts.h"
#include "../armdefs.h"

#include "i2c.h"  

struct IOCStruct {
  unsigned char ControlReg;
  unsigned char ControlRegInputData;
  unsigned char SerialRxData;
  unsigned char SerialTxData;
  unsigned int IRQStatus,IRQMask;
  unsigned int FIRQStatus,FIRQMask;
  int TimerCount[4];
  unsigned int TimerInputLatch[4];
  unsigned int TimerOutputLatch[4];

  struct {
    unsigned int insidebitcount;
    unsigned int bitinbytecount;
    unsigned char CurrentKeyVal;
  } kbd;

  struct I2CStruct i2c;

  unsigned int LatchA;     /* NOTE: int's to allow -1 at startup */
  unsigned int LatchAold;
  unsigned int LatchB;
  unsigned int LatchBold;

  unsigned long TimersLastUpdated;
  unsigned long NextTimerTrigger;
  unsigned int Timer0CanInt;
  unsigned int Timer1CanInt;
};

extern struct IOCStruct ioc;

#define TIMERSCALE 2
#define IOCTIMERSTEP 1

/*------------------------------------------------------------------------------*/
void IOC_DoIntCheck(ARMul_State *state);

/*-----------------------------------------------------------------------------*/
void IO_Init(ARMul_State *state);

/*------------------------------------------------------------------------------*/
void IOC_UpdateTimers(void);

/*-----------------------------------------------------------------------------*/
ARMword GetWord_IO(ARMul_State *state, ARMword address);

/*-----------------------------------------------------------------------------*/
void PutValIO(ARMul_State *state,ARMword address, ARMword data,int bNw);

/*-----------------------------------------------------------------------------*/
int IOC_WriteKbdRx(ARMul_State *state, unsigned char value);
/*-----------------------------------------------------------------------------*/
int IOC_ReadKbdTx(ARMul_State *state);

void UpdateTimerRegisters(void);
void IO_UpdateNfiq(void);
void IO_UpdateNirq(void);

#endif
