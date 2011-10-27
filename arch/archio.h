#ifndef ARCHIO_HEADER
#define ARCHIO_HEADER
/* (c) David Alan Gilbert 1995-1998 - see Readme file for copying info */

#include <signal.h>
#include "../armdefs.h"

struct IOCStruct {
  uint8_t ControlReg;
  uint8_t ControlRegInputData;
  uint8_t SerialRxData;
  uint8_t SerialTxData;
  uint8_t IOEBControlReg;
  uint16_t IRQStatus,IRQMask;
  uint16_t FIRQStatus,FIRQMask;
  int32_t TimerCount[4];
  uint16_t TimerInputLatch[4];
  uint16_t TimerOutputLatch[4];

  struct {
    unsigned int insidebitcount;
    unsigned int bitinbytecount;
    unsigned char CurrentKeyVal;
  } kbd;

  unsigned int LatchA;     /* NOTE: int's to allow -1 at startup */
  unsigned int LatchAold;
  unsigned int LatchB;
  unsigned int LatchBold;

  CycleCount TimersLastUpdated;
  CycleCount NextTimerTrigger;
  uint16_t TimerFracBit;
  bool Timer0CanInt;
  bool Timer1CanInt;

  uint32_t IOCRate; /* Number of IOC clock ticks per emu cycle, in 16.16 fixed-point format */
  uint32_t InvIOCRate; /* Inverse IOC rate, 16.16 */
};

extern struct IOCStruct ioc;


#define IRQA_VFLYBK (1U << 3)   /* Start of display vertical flyback */
#define IRQA_POR    (1U << 4)   /* Power-on reset has occurred */
#define IRQA_TM0    (1U << 5)   /* Timer 0 event, latched */
#define IRQA_TM1    (1U << 6)   /* Timer 1 event, latched */
#define IRQA_FORCE  (1U << 7)   /* Software generated IRQ */

#define IRQB_PFIQ   (1U << 8)   /* Podule FIQ request */
#define IRQB_SIRQ   (1U << 9)   /* Sound buffer pointer used */
#define IRQB_SINTR  (1U << 10)  /* Serial line interrupt */
#define IRQB_HDIRQ  (1U << 11)  /* Hard disc interrupt */
#define IRQB_FINTR  (1U << 12)  /* Floppy disc interrupt */
#define IRQB_PIRQ   (1U << 13)  /* Podule IRQ request */
#define IRQB_STX    (1U << 14)  /* Keyboard transmit register empty */
#define IRQB_SRX    (1U << 15)  /* Keyboard receive register full */

#define FIQ_FDDRQ   (1U << 0)   /* Floppy Disc Data Request */
#define FIQ_FDIRQ   (1U << 1)   /* Floppy Disc Interrupt Request */
#define FIQ_EFIQ    (1U << 2)   /* Econet Interrupt Request */
#define FIQ_PFIQ    (1U << 6)   /* Podule FIQ request */
#define FIQ_FORCE   (1U << 7)   /* Software generated FIQ */


/* IOEB control reg bits */
#define IOEB_CR_VIDC_MASK   (0x3)
#define IOEB_CR_VIDC_24MHz  (0x0)
#define IOEB_CR_VIDC_25MHz  (0x1)
#define IOEB_CR_VIDC_36MHz  (0x2)
#define IOEB_CR_VIDC_24MHzB (0x3) /* Two settings will produce a 24MHz clock */
#define IOEB_CR_EORH        (0x4)
#define IOEB_CR_EORV        (0x8)

/*------------------------------------------------------------------------------*/
void IOC_DoIntCheck(ARMul_State *state);

/*-----------------------------------------------------------------------------*/
void IO_Init(ARMul_State *state);

/*-----------------------------------------------------------------------------*/
ARMword GetWord_IO(ARMul_State *state, ARMword address);

/*-----------------------------------------------------------------------------*/
void PutValIO(ARMul_State *state,ARMword address, ARMword data,bool bNw);

/*-----------------------------------------------------------------------------*/
int IOC_WriteKbdRx(ARMul_State *state, uint8_t value);
/*-----------------------------------------------------------------------------*/
int IOC_ReadKbdTx(ARMul_State *state);

void UpdateTimerRegisters(ARMul_State *state);
void IO_UpdateNfiq(ARMul_State *state);
void IO_UpdateNirq(ARMul_State *state);

#endif
