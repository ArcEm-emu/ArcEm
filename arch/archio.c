/* archio.c - IO and IOC emulation.  (c) David Alan Gilbert 1995 */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */

#include <ctype.h>
#include <signal.h>

#include "../armopts.h"
#include "../armdefs.h"

#include "armarc.h"
#include "fdc1772.h"
#include "hdc63463.h"
#include "i2c.h"  

/*#define IOC_TRACE*/

struct IOCStruct ioc;

/*-----------------------------------------------------------------------------*/
void IO_Init(ARMul_State *state) {
  ioc.ControlReg=0xff; 
  ioc.ControlRegInputData=0x7f; /* Not sure about IF and IR pins */
  ioc.IRQStatus=0x0090; /* (A) Top bit always set - plus power on reset */
  ioc.IRQMask=0;
  ioc.FIRQStatus=0;
  ioc.FIRQMask=0;
  ioc.kbd.CurrentKeyVal=0xff;
  ioc.LatchA=ioc.LatchB=0xff;
  ioc.LatchAold=ioc.LatchBold=-1;
  ioc.TimerInputLatch[0]=0xffff;
  ioc.TimerInputLatch[1]=0xffff;
  ioc.TimerInputLatch[2]=0xffff;
  ioc.TimerInputLatch[3]=0xffff;
  ioc.Timer0CanInt=ioc.Timer1CanInt=1;
  ioc.TimersLastUpdated=-1;
  ioc.NextTimerTrigger=0;

  IO_UpdateNirq();
  IO_UpdateNfiq();

  I2C_Init(state);
  FDC_Init(state);
  HDC_Init(state);

}; /* IO_Init */

/*------------------------------------------------------------------------------*/
void IO_UpdateNfiq(void) {
  register unsigned int tmp=statestr.Exception & ~2;
  
  if (ioc.FIRQStatus & ioc.FIRQMask) {
    /* Cause FIQ */
    tmp |=2;
  };

  statestr.Exception=tmp;
}

/*------------------------------------------------------------------------------*/
void IO_UpdateNirq(void) {
  register unsigned int tmp=statestr.Exception & ~1;

  if (ioc.IRQStatus & ioc.IRQMask) {
    /* Cause interrupt! */
    tmp|=1;
  };

  statestr.Exception=tmp;
}

/*------------------------------------------------------------------------------*/
/* Calculate if Timer0 or Timer1 can cause an interrupt; if either of them
   suddenly become able to then call UpdateTimerRegisters to fix an event
   to cause the interrupt later */
static void CalcCanTimerInt(void) {
  int oldTimer0CanInt=ioc.Timer0CanInt;
  int oldTimer1CanInt=ioc.Timer1CanInt;

  /* If its not causing an interrupt at the moment, and its interrupt is
     enabled */
  ioc.Timer0CanInt=((ioc.IRQStatus & 0x20)==0) && ((ioc.IRQMask & 0x20)!=0);
  ioc.Timer1CanInt=((ioc.IRQStatus & 0x40)==0) && ((ioc.IRQMask & 0x40)!=0);

  /* If any have just been enabled update the triggers */
  if (((!oldTimer0CanInt) && (ioc.Timer0CanInt)) ||
      ((!oldTimer1CanInt) && (ioc.Timer1CanInt)))
    UpdateTimerRegisters();
}; /* CalcCanTimerInt */

/*------------------------------------------------------------------------------*/
/* Get the value of a timer uptodate - don't actually update anything - just    */
/* return the current value (for the Latch command)                             */
static int GetCurrentTimerVal(int toget) {
  long timeSinceLastUpdate=ARMul_Time-ioc.TimersLastUpdated;
  long scaledTimeSlip=timeSinceLastUpdate/TIMERSCALE;
  long tmpL;
  int result;

  tmpL=ioc.TimerInputLatch[toget];
  if (tmpL==0) tmpL=1;
  result=ioc.TimerCount[toget]-(scaledTimeSlip % tmpL);
  if (result<0) result+=tmpL;

  return result;
}


/*------------------------------------------------------------------------------*/
void UpdateTimerRegisters(void) {
  long timeSinceLastUpdate=ARMul_Time-ioc.TimersLastUpdated;
  long scaledTimeSlip=timeSinceLastUpdate/TIMERSCALE;
  unsigned long tmpL;

  ioc.NextTimerTrigger = ARMul_Time + 8192; /* Doesn't let things wrap */

  /* ----------------------------------------------------------------- */
  tmpL=ioc.TimerInputLatch[0];
  if (tmpL==0) tmpL=1;
  if (ioc.TimerCount[0]<=scaledTimeSlip) {
    KBD.TimerIntHasHappened++;
    ioc.IRQStatus|=0x20;
    IO_UpdateNirq();
    ioc.Timer0CanInt=0; /* Because it's just caused one which hasn't cleared yet */
  };
  ioc.TimerCount[0]-=(scaledTimeSlip % tmpL);
  if (ioc.TimerCount[0]<0) ioc.TimerCount[0]+=tmpL;

  if (ioc.Timer0CanInt) {
    tmpL=(ioc.TimerCount[0]*TIMERSCALE)+ARMul_Time;
    if (tmpL<ioc.NextTimerTrigger) ioc.NextTimerTrigger=tmpL;
  };

  /* ----------------------------------------------------------------- */
  tmpL=ioc.TimerInputLatch[1];
  if (tmpL==0) tmpL=1;
  if (ioc.TimerCount[1]<=scaledTimeSlip) {
    ioc.IRQStatus|=0x40;
    IO_UpdateNirq();
    ioc.Timer1CanInt=0; /* Because its just caused one which hasn't cleared yet */
  };
  ioc.TimerCount[1]-=(scaledTimeSlip % tmpL);
  if (ioc.TimerCount[1]<0) ioc.TimerCount[1]+=tmpL;

  if (ioc.Timer1CanInt) {
    tmpL=(ioc.TimerCount[1]*TIMERSCALE)+ARMul_Time;
    if (tmpL<ioc.NextTimerTrigger) ioc.NextTimerTrigger=tmpL;
  };

  /* ----------------------------------------------------------------- */
  if (ioc.TimerInputLatch[2]) {
    ioc.TimerCount[2]-=(scaledTimeSlip % (ioc.TimerInputLatch[2]));
  };

  /* ----------------------------------------------------------------- */
  if (ioc.TimerInputLatch[3]) {
    ioc.TimerCount[3]-=(scaledTimeSlip % (ioc.TimerInputLatch[3]));
  };

  ioc.TimersLastUpdated=ARMul_Time;
}

/*------------------------------------------------------------------------------*/
/* Called when there has been a write to the IOC control register - this
   potentially affects the values on the C0-C5 control lines

   C0 is I2C clock, C1 is I2C data */
static void IOC_ControlLinesUpdate(ARMul_State *state) {

#ifdef IOC_TRACE
  fprintf(stderr,"IOC_ControlLines: Clk=%d Data=%d\n",(ioc.ControlReg & 2)!=0,
    ioc.ControlReg & 1);
#endif
  I2C_Update(state);

}; /* IOC_ControlLinesUpdate */

/*-----------------------------------------------------------------------------*/
/* Parse an IOC address - returns the chip select                              */
static int ParseIOCAddr(ARMword address, int *bank, int *speed, int *offset) {
  int CS=(address & (1<<21))?1:0;


  *bank=(address >> 16) & 7;
  *speed=(address >> 19) & 3;

  *offset=address & 0xffff;

  /*fprintf(stderr,"ParseIOCAddr: address=0x%x bank=%d speed=%d offset=0x%x\n",
          address,*bank,*speed,*offset); */

  if (!CS) return(0); /* Don't do anything to bank etc. */
  return(1);
}; /* ParseIOCAddr */

/*-----------------------------------------------------------------------------*/
static ARMword GetWord_IOCReg(ARMul_State *state, int Register) {
  ARMword Result;
  int Timer;

  switch (Register) {
    case 0: /* Control reg */
      Result=ioc.ControlRegInputData & ioc.ControlReg;
#ifdef IOC_TRACE
      fprintf(stderr,"IOCRead: ControlReg=0x%x\n",(unsigned int)Result);
#endif
      break;

    case 1: /* Serial Rx data */
      Result=ioc.SerialRxData;
      ioc.IRQStatus &=0x7fff; /* Clear receive reg full */
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: SerialRxData=0x%x\n",(unsigned int)Result);
#endif   
      IO_UpdateNirq();
      break;

    case 4: /* IRQ Status A */
      Result=ioc.IRQStatus & 0xff;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: IRQStatusA=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 5: /* IRQ Request A */
      Result=(ioc.IRQStatus & ioc.IRQMask) & 0xff;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: IRQRequestA=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 6: /* IRQ Mask A */
      Result=ioc.IRQMask & 0xff;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: IRQMaskA=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 8: /* IRQ Status B */
      Result=ioc.IRQStatus >> 8;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: IRQStatusB=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 9: /* IRQ Request B */
      Result=(ioc.IRQStatus & ioc.IRQMask) >> 8;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: IRQRequestB=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 0xa: /* IRQ Mask B */
      Result=ioc.IRQMask >> 8;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: IRQMaskB=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 0xc: /* FIRQ Status */
      Result=ioc.FIRQStatus;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: FIRQStatus=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 0xd: /* FIRQ Request */
      Result=ioc.FIRQStatus & ioc.FIRQMask;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: FIRQRequest=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 0xe: /* FIRQ mask */
      Result=ioc.FIRQMask;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: FIRQMask=0x%x\n",(unsigned int)Result);
#endif   
      break;

    case 0x10: /* T0 count low */
    case 0x14: /* T1 count low */
    case 0x18: /* T2 count low */
    case 0x1c: /* T3 count low */
      Timer=(Register & 0xf)/4;
      Result=ioc.TimerOutputLatch[Timer] & 0xff;
#ifdef IOC_TRACE   
      /*fprintf(stderr,"IOCRead: Timer %d low counter read=0x%x\n",Timer,(unsigned int)Result);
      fprintf(stderr,"SPECIAL: R0=0x%x R1=0x%x R14=0x%x\n",statestr.Reg[0],
              statestr.Reg[1],statestr.Reg[14]); */
#endif       
      break;

    case 0x11: /* T0 count high */
    case 0x15: /* T1 count high */
    case 0x19: /* T2 count high */
    case 0x1a: /* T3 count high */
      Timer=(Register & 0xf)/4;
      Result=(ioc.TimerOutputLatch[Timer]/256) & 0xff;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOCRead: Timer %d high counter read=0x%x\n",Timer,(unsigned int)Result);
#endif   
      break;

    default:
      Result = 0;
      fprintf(stderr,"IOCRead: Read of unknown IOC register %d\n",Register);
      break;
  };

  return(Result);
}; /* GetWord_IOCReg */

/*-----------------------------------------------------------------------------*/
static ARMword PutVal_IOCReg(ARMul_State *state, int Register, ARMword data, int bNw) {
  int Timer;

  switch (Register) {
    case 0: /* Control reg */
      ioc.ControlReg = (data & 0x3f) | 0xc0; /* Needs more work */
      IOC_ControlLinesUpdate(state);
#ifdef IOC_TRACE   
      fprintf(stderr,"IOC Write: Control reg val=0x%x\n",(unsigned int)data);
#endif   
      break;

    case 1: /* Serial Tx Data */
      ioc.SerialTxData=data; /* Should tell the keyboard about this */
      ioc.IRQStatus &=0xbfff; /* Clear KART Tx empty */
#ifdef IOC_TRACE   
      fprintf(stderr,"IOC Write: Serial Tx Reg Val=0x%x\n",(unsigned int)data);
#endif   
      IO_UpdateNirq();
      break;

    case 5: /* IRQ Clear */
#ifdef IOC_TRACE  
      fprintf(stderr,"IOC Write: Clear Ints Val=0x%x\n",(unsigned int)data);
#endif   
      /* Clear appropriate interrupts */
      data&=0x7c;
      ioc.IRQStatus&=~data;
      /* If we have cleared a timer interrupt then it may cause another */
      if (data & 0x60) CalcCanTimerInt();
      IO_UpdateNirq();
      break;

    case 6: /* IRQ Mask A */
      ioc.IRQMask=ioc.IRQMask & 0xff00;
      ioc.IRQMask|=(data & 0xff);
      CalcCanTimerInt();
#ifdef IOC_TRACE   
      fprintf(stderr,"IOC Write: IRQ Mask A Val=0x%x\n",(unsigned int)data);
#endif   
      IO_UpdateNirq();
      break;
      
    case 0xa: /* IRQ mask B */
      ioc.IRQMask&=0xff;
      ioc.IRQMask|=(data &0xff)<<8;
#ifdef IOC_TRACE   
      fprintf(stderr,"IOC Write: IRQ Mask B Val=0x%x\n",(unsigned int)data);
#endif   
      IO_UpdateNirq();
      break;
      
    case 0xe: /* FIRQ Mask */
      ioc.FIRQMask=data;
      IO_UpdateNfiq();
#ifdef IOC_TRACE   
      fprintf(stderr,"IOC Write: FIRQ Mask Val=0x%x\n",(unsigned int)data);
#endif   
      break;
      
    case 0x10: /* T0 latch low */
    case 0x14: /* T1 latch low */
    case 0x18: /* T2 latch low */
    case 0x1c: /* T3 latch low */
      Timer=(Register & 0xf)/4;
      UpdateTimerRegisters();
      ioc.TimerInputLatch[Timer]&=0xff00;
      ioc.TimerInputLatch[Timer]|=data;
      UpdateTimerRegisters();
#ifdef IOC_TRACE
      fprintf(stderr,"IOC Write: Timer %d latch write low Val=0x%x InpLatch=0x%x\n",
              Timer,data,ioc.TimerInputLatch[Timer]);
#endif
      break;

    case 0x11: /* T0 latch High */
    case 0x15: /* T1 latch High */
    case 0x19: /* T2 latch High */
    case 0x1d: /* T3 latch High */
      Timer=(Register & 0xf)/4;
      UpdateTimerRegisters();
      ioc.TimerInputLatch[Timer]&=0xff;
      ioc.TimerInputLatch[Timer]|=data<<8;
      UpdateTimerRegisters();
#ifdef IOC_TRACE
      fprintf(stderr,"IOC Write: Timer %d latch write high Val=0x%x InpLatch=0x%x\n",
              Timer,data,ioc.TimerInputLatch[Timer]);
#endif
      break;

    case 0x12: /* T0 Go */
    case 0x16: /* T1 Go */
    case 0x1a: /* T2 Go */
    case 0x1e: /* T3 Go */
      Timer=(Register & 0xf)/4;
      UpdateTimerRegisters();
      ioc.TimerCount[Timer]= ioc.TimerInputLatch[Timer];
      UpdateTimerRegisters();
#ifdef IOC_TRACE
      fprintf(stderr,"IOC Write: Timer %d Go! Counter=0x%x\n",
              Timer,ioc.TimerCount[Timer]);
#endif
      break;

    case 0x13: /* T0 Latch command */
    case 0x17: /* T1 Latch command */
    case 0x1b: /* T2 Latch command */
    case 0x1f: /* T3 Latch command */
      Timer=(Register & 0xf)/4;
      ioc.TimerOutputLatch[Timer]=GetCurrentTimerVal(Timer) & 0xffff;
      /*fprintf(stderr,"(T%dLc)",Timer); */
      /*fprintf(stderr,"IOC Write: Timer %d Latch command Output Latch=0x%x\n",
        Timer, ioc.TimerOutputLatch[Timer]); */
      break;

    default:
      fprintf(stderr,"IOC Write: Bad IOC register write reg=%d data=0x%d\n",
              Register,data);
      break;
  }; /* Register */

  return 0;
}; /* PutVal_IOCReg */

/*-----------------------------------------------------------------------------*/
ARMword GetWord_IO(ARMul_State *state, ARMword address) {
  int bank,speed,offset;
  /* Sanitise the address a bit */
  address-=0x3000000;
  
  if (!ParseIOCAddr(address,&bank,&speed,&offset)) {
    /*EnableTrace();*/
    fprintf(stderr,"Read from non-IOC IO space (addr=0x%x bank=0x%x speed=0x%x offset=0x%x pc=0x%x\n\n",
                    (unsigned int)address+0x3000000,bank,speed,offset,statestr.pc);

    return(0);
  } else {
    /* OK - its IOC space */
    switch (bank) {
      case 0:
        offset/=4;
        offset&=0x1f;
        return(GetWord_IOCReg(state,offset));
        break;

      case 1:
        return(FDC_Read(state, offset));
        break;

      case 2:
#ifdef IOC_TRACE
        fprintf(stderr,"Read from Econet register (addr=0x%x speed=0x%x offset=0x%x)\n",
                        (unsigned int)address,speed,offset);
#endif
        break;

      case 3:
#ifdef IOC_TRACE
        fprintf(stderr,"Read from Serial port register (addr=0x%x speed=0x%x offset=0x%x)\n",
                        (unsigned int)address,speed,offset);
#endif
        break;

      case 4:
#ifdef IOC_TRACE
        fprintf(stderr,"Read from Internal expansion card (addr=0x%x speed=0x%x offset=0x%x)\n",
                        (unsigned int)address,speed,offset);
#endif
        break;

      case 5:
        /*fprintf(stderr,"HDC read: address=0x%x speed=%d\n",address,speed); */
        return(HDC_Read(state,offset));
        break;
        
      case 6:
#ifdef IOC_TRACE
        fprintf(stderr,"Read from Bank 6 (addr=0x%x speed=0x%x offset=0x%x)\n",
                        (unsigned int)address,speed,offset);
#endif
        break;

      case 7:
#ifdef IOC_TRACE
        fprintf(stderr,"Read from External expansion card (addr=0x%x speed=0x%x offset=0x%x)\n",
                        (unsigned int)address,speed,offset);
#endif
        break;
    }; /* Bank switch */
  }; /* Yep - IOC space */

  return 0xffffff; /* was 0 */
}; /* GetWord_IO */

/*-----------------------------------------------------------------------------*/
void PutValIO(ARMul_State *state,ARMword address, ARMword data,int bNw) {
  int bank,speed,offset;
  /* Sanitise the address a bit */
  address-=0x3000000;
  
  if (!ParseIOCAddr(address,&bank,&speed,&offset)) {
    fprintf(stderr,"Write to non-IOC IO space (addr=0x%x bank=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                    (unsigned int)address,bank,speed,offset,(unsigned int)data);
  } else {
    /* OK - its IOC space */
    if (!bNw) {
      /* The data bus is wierdly connected to the IOC - in the case of word
         writes, bits 16-23 of the data bus end up on the IOC data bus */
      data=data>>16;
      data &=0xffff;
    } else {
      /* Just 8 bits actually go to the IOC */
      data &=0xff;
    };

    switch (bank) {
      case 0:
        offset/=4;
        offset&=0x1f;
        PutVal_IOCReg(state,offset,data & 0xff,bNw);
        break;

      case 1:
        FDC_Write(state,offset,data & 0xff,bNw);
        break;

#ifdef IOC_TRACE
      case 2:
        fprintf(stderr,"Write to Econet register (addr=0x%x speed=0x%x offset=0x%x data=0x%x %c\n",
                        (unsigned int)address,speed,offset,(unsigned int)data,
                        isprint((data & 0xff))?data:32);
        break;

      case 3:
        fprintf(stderr,"Write to Serial port register (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                        (unsigned int)address,speed,offset,(unsigned int)data);
        break;

      case 4:
        fprintf(stderr,"Write to Internal expansion card (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                        (unsigned int)address,speed,offset,(unsigned int)data);
        break;
#endif

      case 5:
        /* It's either Latch A/B, printer DATA  or the HDC */
        switch (offset & 0x58) {
          case 0x00:
            /*fprintf(stderr,"HDC write: address=0x%x speed=%d\n",address,speed); */
            HDC_Write(state,offset,data);
            break;

          case 0x08:
            HDC_Write(state,offset,data);
            break;

          case 0x10:
#ifdef IOC_TRACE
            fprintf(stderr,"Write to Printer data latch offset=0x%x data=0x%x\n",offset,(unsigned int)data);
#endif
            break;

          case 0x18:
#ifdef IOC_TRACE
            fprintf(stderr,"Write to Latch B offset=0x%x data=0x%x\n",offset,(unsigned int)data);
#endif
            ioc.LatchB=data & 0xff;
            FDC_LatchBChange(state);
            ioc.LatchBold=data & 0xff;
            break;

          case 0x40:
#ifdef IOC_TRACE
            fprintf(stderr,"Write to Latch A offset=0x%x data=0x%x\n",offset,(unsigned int)data);
#endif
            ioc.LatchA=data & 0xff;
            FDC_LatchAChange(state);
            ioc.LatchAold=data & 0xff;
            break;


          default:
#ifdef IOC_TRACE
            fprintf(stderr,"Writing to non-defined bank 5 address offset=0x%x data=0x%x\n",
                    offset,(unsigned int)data);
#endif
            break;
        };
        break;
        
#ifdef IOC_TRACE
      case 6:
        fprintf(stderr,"Write to Bank 6 (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                        (unsigned int)address,speed,offset,(unsigned int)data);
        break;

      case 7:
        fprintf(stderr,"Write to External expansion card (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                        (unsigned int)address,speed,offset,(unsigned int)data);
        break;
#endif
    }; /* Bank switch */
  }; /* Yep - IOC space */
}; /* PutValIO */

/* ------------------- access routines for other sections of code ------------- */
/* Returns a byte in the keyboard serialiser tx register - or -1 if there isn't
   one. Clears the tx register full flag                                        */
int IOC_ReadKbdTx(ARMul_State *state) {
  if ((ioc.IRQStatus & 0x4000)==0) {
    /*fprintf(stderr,"ArmArc_ReadKbdTx: Value=0x%x\n",ioc.SerialTxData); */
    /* There is a byte present (Kart TX not empty) */
    /* Mark as empty and then return the value */
    ioc.IRQStatus|=0x4000;
    IO_UpdateNirq();
    return(ioc.SerialTxData);
  } else return(-1);
}; /* IOC_ReadKbdTx */

/*------------------------------------------------------------------------------*/
/* Places a byte in the keyboard serialiser rx register; returns -1 if its 
   still full.                                                                  */
int IOC_WriteKbdRx(ARMul_State *state, unsigned char value) {
  /*fprintf(stderr,"ArmArc_WriteKbdRx: value=0x%x\n",value); */
  if (ioc.IRQStatus & 0x8000) {
    /* Still full */
    return(-1);
  } else {
    /* We write only if it was empty */
    ioc.SerialRxData=value;

    ioc.IRQStatus|=0x8000; /* Now full */
    IO_UpdateNirq();
  }

  return(0);
}; /* IOC_WriteKbdRx */
