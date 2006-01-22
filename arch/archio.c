/* archio.c - IO and IOC emulation.  (c) David Alan Gilbert 1995 */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
/*
 * $Id$
 */

#include <ctype.h>
#include <signal.h>

#include "../armopts.h"
#include "../armdefs.h"

#include "dbugsys.h"

#include "armarc.h"
#include "fdc1772.h"
#include "hdc63463.h"
#include "i2c.h"

/*#define IOC_TRACE*/

struct IOCStruct ioc;

/*-----------------------------------------------------------------------------*/
void
IO_Init(ARMul_State *state)
{
  ioc.ControlReg = 0xff;
  ioc.ControlRegInputData = 0x7f; /* Not sure about IF and IR pins */
  ioc.IRQStatus = 0x0090; /* (A) Top bit always set - plus power on reset */
  ioc.IRQMask = 0;
  ioc.FIRQStatus = 0;
  ioc.FIRQMask = 0;
  ioc.kbd.CurrentKeyVal = 0xff;
  ioc.LatchA = ioc.LatchB = 0xff;
  ioc.LatchAold = ioc.LatchBold = -1;
  ioc.TimerInputLatch[0] = 0xffff;
  ioc.TimerInputLatch[1] = 0xffff;
  ioc.TimerInputLatch[2] = 0xffff;
  ioc.TimerInputLatch[3] = 0xffff;
  ioc.Timer0CanInt = ioc.Timer1CanInt = 1;
  ioc.TimersLastUpdated = -1;
  ioc.NextTimerTrigger = 0;

  IO_UpdateNirq();
  IO_UpdateNfiq();

  I2C_Init(state);
  FDC_Init(state);
  HDC_Init(state);
} /* IO_Init */

/*------------------------------------------------------------------------------*/
void
IO_UpdateNfiq(void)
{
  register unsigned int tmp = statestr.Exception & ~2;

  if (ioc.FIRQStatus & ioc.FIRQMask) {
    /* Cause FIQ */
    tmp |= 2;
  }

  statestr.Exception = tmp;
}

/*------------------------------------------------------------------------------*/
void
IO_UpdateNirq(void)
{
  register unsigned int tmp = statestr.Exception & ~1;

  if (ioc.IRQStatus & ioc.IRQMask) {
    /* Cause interrupt! */
    tmp |= 1;
  }

  statestr.Exception = tmp;
}

/** Calculate if Timer0 or Timer1 can cause an interrupt; if either of them
 *  suddenly become able to then call UpdateTimerRegisters to fix an event
 *  to cause the interrupt later */
static void
CalcCanTimerInt(void)
{
  int oldTimer0CanInt = ioc.Timer0CanInt;
  int oldTimer1CanInt = ioc.Timer1CanInt;

  /* If its not causing an interrupt at the moment, and its interrupt is
     enabled */
  ioc.Timer0CanInt = ((ioc.IRQStatus & 0x20) == 0) &&
                     ((ioc.IRQMask & 0x20) != 0);
  ioc.Timer1CanInt = ((ioc.IRQStatus & 0x40) == 0) &&
                     ((ioc.IRQMask & 0x40) != 0);

  /* If any have just been enabled update the triggers */
  if (((!oldTimer0CanInt) && (ioc.Timer0CanInt)) ||
      ((!oldTimer1CanInt) && (ioc.Timer1CanInt)))
    UpdateTimerRegisters();
} /* CalcCanTimerInt */

/** Get the value of a timer uptodate - don't actually update anything - just
 * return the current value (for the Latch command)
 */
static int
GetCurrentTimerVal(int toget)
{
  long timeSinceLastUpdate = ARMul_Time - ioc.TimersLastUpdated;
  long scaledTimeSlip = timeSinceLastUpdate / TIMERSCALE;
  long tmpL;
  int result;

  tmpL = ioc.TimerInputLatch[toget];
  if (tmpL == 0) tmpL = 1;
  result = ioc.TimerCount[toget] - (scaledTimeSlip % tmpL);
  if (result < 0) result += tmpL;

  return result;
}


/*------------------------------------------------------------------------------*/
void
UpdateTimerRegisters(void)
{
  long timeSinceLastUpdate = ARMul_Time - ioc.TimersLastUpdated;
  long scaledTimeSlip = timeSinceLastUpdate / TIMERSCALE;
  unsigned long tmpL;

  ioc.NextTimerTrigger = ARMul_Time + 8192; /* Doesn't let things wrap */

  /* ----------------------------------------------------------------- */
  tmpL = ioc.TimerInputLatch[0];
  if (tmpL == 0) tmpL = 1;
  if (ioc.TimerCount[0] <= scaledTimeSlip) {
    KBD.TimerIntHasHappened++;
    ioc.IRQStatus |= 0x20;
    IO_UpdateNirq();
    ioc.Timer0CanInt = 0; /* Because it's just caused one which hasn't cleared yet */
  }
  ioc.TimerCount[0] -= (scaledTimeSlip % tmpL);
  if (ioc.TimerCount[0] < 0) ioc.TimerCount[0] += tmpL;

  if (ioc.Timer0CanInt) {
    tmpL = (ioc.TimerCount[0] * TIMERSCALE) + ARMul_Time;
    if (tmpL < ioc.NextTimerTrigger) ioc.NextTimerTrigger = tmpL;
  }

  /* ----------------------------------------------------------------- */
  tmpL = ioc.TimerInputLatch[1];
  if (tmpL == 0) tmpL = 1;
  if (ioc.TimerCount[1] <= scaledTimeSlip) {
    ioc.IRQStatus |= 0x40;
    IO_UpdateNirq();
    ioc.Timer1CanInt = 0; /* Because its just caused one which hasn't cleared yet */
  }
  ioc.TimerCount[1] -= (scaledTimeSlip % tmpL);
  if (ioc.TimerCount[1] < 0) ioc.TimerCount[1] += tmpL;

  if (ioc.Timer1CanInt) {
    tmpL = (ioc.TimerCount[1] * TIMERSCALE) + ARMul_Time;
    if (tmpL < ioc.NextTimerTrigger) ioc.NextTimerTrigger = tmpL;
  }

  /* ----------------------------------------------------------------- */
  if (ioc.TimerInputLatch[2]) {
    ioc.TimerCount[2] -= (scaledTimeSlip % ioc.TimerInputLatch[2]);
  }

  /* ----------------------------------------------------------------- */
  if (ioc.TimerInputLatch[3]) {
    ioc.TimerCount[3] -= (scaledTimeSlip % ioc.TimerInputLatch[3]);
  }

  ioc.TimersLastUpdated = ARMul_Time;
}

/** Called when there has been a write to the IOC control register - this
 *  potentially affects the values on the C0-C5 control lines
 *
 *  C0 is I2C clock, C1 is I2C data
 */
static void
IOC_ControlLinesUpdate(ARMul_State *state)
{

  dbug_ioc("IOC_ControlLines: Clk=%d Data=%d\n", (ioc.ControlReg & 2) != 0,
           ioc.ControlReg & 1);
  I2C_Update(state);

} /* IOC_ControlLinesUpdate */

/** Parse an IOC address
 *
 * \param address Address (assumed to be in IO-space)
 * \param bank    Return IOC bank
 * \param speed   Return IOC speed
 * \param offset  Return IOC offset
 */
static void
ParseIOCAddr(ARMword address, ARMword *bank, ARMword *speed, ARMword *offset)
{
  *bank   = (address >> 16) & 7;
  *speed  = (address >> 19) & 3;

  *offset = address & 0xffff;

  /*fprintf(stderr, "ParseIOCAddr: address=0x%x bank=%u speed=%u offset=0x%x\n",
          address, *bank, *speed, *offset); */
} /* ParseIOCAddr */

/*-----------------------------------------------------------------------------*/
static ARMword
GetWord_IOCReg(ARMul_State *state, int Register)
{
  ARMword Result;
  int Timer;

  switch (Register) {
    case 0: /* Control reg */
      Result = ioc.ControlRegInputData & ioc.ControlReg;
      dbug_ioc("IOCRead: ControlReg=0x%x\n", Result);
      break;

    case 1: /* Serial Rx data */
      Result = ioc.SerialRxData;
      ioc.IRQStatus &= 0x7fff; /* Clear receive reg full */
      dbug_ioc("IOCRead: SerialRxData=0x%x\n", Result);
      IO_UpdateNirq();
      break;

    case 4: /* IRQ Status A */
      Result = ioc.IRQStatus & 0xff;
      dbug_ioc("IOCRead: IRQStatusA=0x%x\n", Result);
      break;

    case 5: /* IRQ Request A */
      Result = (ioc.IRQStatus & ioc.IRQMask) & 0xff;
      dbug_ioc("IOCRead: IRQRequestA=0x%x\n", Result);
      break;

    case 6: /* IRQ Mask A */
      Result = ioc.IRQMask & 0xff;
      dbug_ioc("IOCRead: IRQMaskA=0x%x\n", Result);
      break;

    case 8: /* IRQ Status B */
      Result = ioc.IRQStatus >> 8;
      dbug_ioc("IOCRead: IRQStatusB=0x%x\n", Result);
      break;

    case 9: /* IRQ Request B */
      Result = (ioc.IRQStatus & ioc.IRQMask) >> 8;
      dbug_ioc("IOCRead: IRQRequestB=0x%x\n", Result);
      break;

    case 0xa: /* IRQ Mask B */
      Result = ioc.IRQMask >> 8;
      dbug_ioc("IOCRead: IRQMaskB=0x%x\n", Result);
      break;

    case 0xc: /* FIRQ Status */
      Result = ioc.FIRQStatus;
      dbug_ioc("IOCRead: FIRQStatus=0x%x\n", Result);
      break;

    case 0xd: /* FIRQ Request */
      Result = ioc.FIRQStatus & ioc.FIRQMask;
      dbug_ioc("IOCRead: FIRQRequest=0x%x\n", Result);
      break;

    case 0xe: /* FIRQ mask */
      Result = ioc.FIRQMask;
      fprintf(stderr,"IOCRead: FIRQMask=0x%x\n", Result);
      break;

    case 0x10: /* T0 count low */
    case 0x14: /* T1 count low */
    case 0x18: /* T2 count low */
    case 0x1c: /* T3 count low */
      Timer = (Register & 0xf) / 4;
      Result = ioc.TimerOutputLatch[Timer] & 0xff;
      /*dbug_ioc("IOCRead: Timer %d low counter read=0x%x\n", Timer, Result);
      dbug_ioc("SPECIAL: R0=0x%x R1=0x%x R14=0x%x\n", statestr.Reg[0],
              statestr.Reg[1], statestr.Reg[14]); */
      break;

    case 0x11: /* T0 count high */
    case 0x15: /* T1 count high */
    case 0x19: /* T2 count high */
    case 0x1a: /* T3 count high */
      Timer = (Register & 0xf) / 4;
      Result = (ioc.TimerOutputLatch[Timer] / 256) & 0xff;
      dbug_ioc("IOCRead: Timer %d high counter read=0x%x\n", Timer, Result);
      break;

    default:
      Result = 0;
      fprintf(stderr, "IOCRead: Read of unknown IOC register %d\n", Register);
      break;
  }

  return Result;
} /* GetWord_IOCReg */

/*-----------------------------------------------------------------------------*/
static ARMword
PutVal_IOCReg(ARMul_State *state, int Register, ARMword data, int bNw)
{
  int Timer;

  switch (Register) {
    case 0: /* Control reg */
      ioc.ControlReg = (data & 0x3f) | 0xc0; /* Needs more work */
      IOC_ControlLinesUpdate(state);
      dbug_ioc("IOC Write: Control reg val=0x%x\n", data);
      break;

    case 1: /* Serial Tx Data */
      ioc.SerialTxData = data; /* Should tell the keyboard about this */
      ioc.IRQStatus &= 0xbfff; /* Clear KART Tx empty */
      dbug_ioc("IOC Write: Serial Tx Reg Val=0x%x\n", data);
      IO_UpdateNirq();
      break;

    case 5: /* IRQ Clear */
      dbug_ioc("IOC Write: Clear Ints Val=0x%x\n", data);
      /* Clear appropriate interrupts */
      data &= 0x7c;
      ioc.IRQStatus &= ~data;
      /* If we have cleared a timer interrupt then it may cause another */
      if (data & 0x60)
        CalcCanTimerInt();
      IO_UpdateNirq();
      break;

    case 6: /* IRQ Mask A */
      ioc.IRQMask &= 0xff00;
      ioc.IRQMask |= (data & 0xff);
      CalcCanTimerInt();
      dbug_ioc("IOC Write: IRQ Mask A Val=0x%x\n", data);
      IO_UpdateNirq();
      break;

    case 0xa: /* IRQ mask B */
      ioc.IRQMask &= 0xff;
      ioc.IRQMask |= (data & 0xff) << 8;
      dbug_ioc("IOC Write: IRQ Mask B Val=0x%x\n", data);
      IO_UpdateNirq();
      break;

    case 0xe: /* FIRQ Mask */
      ioc.FIRQMask = data;
      IO_UpdateNfiq();
      dbug_ioc("IOC Write: FIRQ Mask Val=0x%x\n", data);
      break;

    case 0x10: /* T0 latch low */
    case 0x14: /* T1 latch low */
    case 0x18: /* T2 latch low */
    case 0x1c: /* T3 latch low */
      Timer = (Register & 0xf) / 4;
      UpdateTimerRegisters();
      ioc.TimerInputLatch[Timer] &= 0xff00;
      ioc.TimerInputLatch[Timer] |= data;
      UpdateTimerRegisters();
      dbug_ioc("IOC Write: Timer %d latch write low Val=0x%x InpLatch=0x%x\n",
              Timer, data, ioc.TimerInputLatch[Timer]);
      break;

    case 0x11: /* T0 latch High */
    case 0x15: /* T1 latch High */
    case 0x19: /* T2 latch High */
    case 0x1d: /* T3 latch High */
      Timer = (Register & 0xf) / 4;
      UpdateTimerRegisters();
      ioc.TimerInputLatch[Timer] &= 0xff;
      ioc.TimerInputLatch[Timer] |= data << 8;
      UpdateTimerRegisters();
      dbug_ioc("IOC Write: Timer %d latch write high Val=0x%x InpLatch=0x%x\n",
              Timer, data, ioc.TimerInputLatch[Timer]);
      break;

    case 0x12: /* T0 Go */
    case 0x16: /* T1 Go */
    case 0x1a: /* T2 Go */
    case 0x1e: /* T3 Go */
      Timer = (Register & 0xf) / 4;
      UpdateTimerRegisters();
      ioc.TimerCount[Timer] = ioc.TimerInputLatch[Timer];
      UpdateTimerRegisters();
      dbug_ioc("IOC Write: Timer %d Go! Counter=0x%x\n",
              Timer, ioc.TimerCount[Timer]);
      break;

    case 0x13: /* T0 Latch command */
    case 0x17: /* T1 Latch command */
    case 0x1b: /* T2 Latch command */
    case 0x1f: /* T3 Latch command */
      Timer = (Register & 0xf) / 4;
      ioc.TimerOutputLatch[Timer] = GetCurrentTimerVal(Timer) & 0xffff;
      /*dbug_ioc("(T%dLc)", Timer); */
      /*dbug_ioc("IOC Write: Timer %d Latch command Output Latch=0x%x\n",
        Timer, ioc.TimerOutputLatch[Timer]); */
      break;

    default:
      fprintf(stderr,"IOC Write: Bad IOC register write reg=%d data=0x%x\n",
              Register, data);
      break;
  } /* Register */

  return 0;
} /* PutVal_IOCReg */

/** Read a word of data from IO address space
 *
 * \param state   Emulator state
 * \param address Address to read from (must be in IO address space)
 * \return Data read
 */
ARMword
GetWord_IO(ARMul_State *state, ARMword address)
{
  /* Test CS (Chip-Select) bit */
  if (address & (1 << 21)) {
    /* IOC address-space */

    ARMword bank, speed, offset;

    /* Sanitise the address a bit */
    address -= MEMORY_0x3000000_CON_IO;

    ParseIOCAddr(address, &bank, &speed, &offset);

    switch (bank) {
      case 0:
        offset /= 4;
        offset &= 0x1f;
        return GetWord_IOCReg(state, offset);

      case 1:
        return FDC_Read(state, offset);

      case 2:
        dbug_ioc("Read from Econet register (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 3:
        dbug_ioc("Read from Serial port register (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 4:
        dbug_ioc("Read from Internal expansion card (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 5:
        /*dbug_ioc("HDC read: address=0x%x speed=%d\n",address,speed); */
        return HDC_Read(state, offset);

      case 6:
        dbug_ioc("Read from Bank 6 (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 7:
        dbug_ioc("Read from External expansion card (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;
    } /* Bank switch */
  } else {
    /* IO-address space unused on Arc hardware */
    /*EnableTrace();*/
    fprintf(stderr, "Read from non-IOC IO space (addr=0x%08x pc=0x%08x\n",
            address, statestr.pc);

    return 0;
  }

  return 0xffffff; /* was 0 */
} /* GetWord_IO */

/** Write a word of data to IO address space
 *
 * \param state       Emulator state
 * \param address     Address to write to (must be in IO address space)
 * \param data        Data to store
 * \param byteNotword Non-zero if this is a byte store, not a word store
 */
void
PutValIO(ARMul_State *state, ARMword address, ARMword data, int byteNotword)
{
  /* Test CS (Chip-Select) bit */
  if (address & (1 << 21)) {
    /* IOC address-space */

    ARMword bank, speed, offset;

    /* Sanitise the address a bit */
    address -= MEMORY_0x3000000_CON_IO;

    ParseIOCAddr(address, &bank, &speed, &offset);

    if (!byteNotword) {
      /* The data bus is wierdly connected to the IOC - in the case of word
      writes, bits 16-23 of the data bus end up on the IOC data bus */
      data >>= 16;
      data &= 0xffff;
    } else {
      /* Just 8 bits actually go to the IOC */
      data &= 0xff;
    }

    switch (bank) {
      case 0:
        offset /= 4;
        offset &= 0x1f;
        PutVal_IOCReg(state, offset, data & 0xff, byteNotword);
        break;

      case 1:
        FDC_Write(state, offset, data & 0xff, byteNotword);
        break;

      case 2:
        dbug_ioc("Write to Econet register (addr=0x%x speed=0x%x offset=0x%x data=0x%x %c\n",
                 address, speed, offset, data,
                 isprint(data & 0xff) ? data : ' ');
        break;

      case 3:
        dbug_ioc("Write to Serial port register (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;

      case 4:
        dbug_ioc("Write to Internal expansion card (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;

      case 5:
        /* It's either Latch A/B, printer DATA  or the HDC */
        switch (offset & 0x58) {
          case 0x00:
            /*fprintf(stderr,"HDC write: address=0x%x speed=%u\n", address, speed); */
            HDC_Write(state, offset, data);
            break;

          case 0x08:
            HDC_Write(state, offset, data);
            break;

          case 0x10:
            dbug_ioc("Write to Printer data latch offset=0x%x data=0x%x\n",
                     offset, data);
            break;

          case 0x18:
            dbug_ioc("Write to Latch B offset=0x%x data=0x%x\n",
                     offset, data);
            ioc.LatchB = data & 0xff;
            FDC_LatchBChange(state);
            ioc.LatchBold = data & 0xff;
            break;

          case 0x40:
            dbug_ioc("Write to Latch A offset=0x%x data=0x%x\n",
                     offset, data);
            ioc.LatchA = data & 0xff;
            FDC_LatchAChange(state);
            ioc.LatchAold = data & 0xff;
            break;

          default:
            dbug_ioc("Writing to non-defined bank 5 address offset=0x%x data=0x%x\n",
                     offset, data);
            break;
        }
        break;

      case 6:
        dbug_ioc("Write to Bank 6 (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;

      case 7:
        dbug_ioc("Write to External expansion card (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;
    } /* Bank switch */
  } else {
    /* IO-address space unused on Arc hardware */
    /* Use it as general-purpose memory-mapped IO space */
    ARMword application = (address >> 12) & 0x1ff;
    ARMword operation   = (address & 0xfff);
    fprintf(stderr, "Write to non-IOC IO space (addr=0x%x app=0x%03x op=0x%03x data=0x%08x\n",
            address, application, operation, data);
  }
} /* PutValIO */

/* ------------------- access routines for other sections of code ------------- */

/** Returns a byte in the keyboard serialiser tx register - or -1 if there isn't
 *  one. Clears the tx register full flag
 */
int
IOC_ReadKbdTx(ARMul_State *state)
{
  if ((ioc.IRQStatus & 0x4000) == 0) {
    /*fprintf(stderr, "ArmArc_ReadKbdTx: Value=0x%x\n", ioc.SerialTxData); */
    /* There is a byte present (Kart TX not empty) */
    /* Mark as empty and then return the value */
    ioc.IRQStatus |= 0x4000;
    IO_UpdateNirq();
    return ioc.SerialTxData;
  } else return -1;
} /* IOC_ReadKbdTx */

/** Places a byte in the keyboard serialiser rx register; returns -1 if its
 *  still full.
 */
int
IOC_WriteKbdRx(ARMul_State *state, unsigned char value)
{
  /*fprintf(stderr, "ArmArc_WriteKbdRx: value=0x%x\n", value); */
  if (ioc.IRQStatus & 0x8000) {
    /* Still full */
    return -1;
  } else {
    /* We write only if it was empty */
    ioc.SerialRxData = value;

    ioc.IRQStatus |= 0x8000; /* Now full */
    IO_UpdateNirq();
  }

  return 0;
} /* IOC_WriteKbdRx */
