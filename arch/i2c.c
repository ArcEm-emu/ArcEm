/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
/* SaveCMOS contributed by Richard York */
#include <signal.h>
#include <stdio.h>
#include "../armopts.h"
#include "../armdefs.h"

#include "armarc.h"
#include "i2c.h"

/*
static const char *I2CStateNameTrans[] = {"Idle",
                                          "GotStart",
                                          "WaitingForSlaveAddr",
                                          "AckAfterSlaveAddr",
                                          "GettingWordAddr",
                                          "AckAfterWordAddr",
                                          "TransmittingToMaster",
                                          "AckAfterTransmitData",
                                          "GettingData",
                                          "AckAfterReceiveData" };
*/

#define I2CDATAREAD ((ioc.ControlReg & (ioc.ControlRegInputData)) & 1)
#define I2CCLOCKREAD ((ioc.ControlReg & 2)!=0)
#define I2CDATAWRITE(v) { /*fprintf(stderr,"I2C data write (%d)\n",v); */ ioc.ControlRegInputData &= ~1; ioc.ControlRegInputData |=v; };
#define I2C (ioc.i2c)

/*------------------------------------------------------------------------------*/
static void I2C_SetupTransmit(ARMul_State *state) {
  /*fprintf(stderr,"I2C_SetupTransmit (address=%d)\n",I2C.WordAddress); */
  I2C.IAmTransmitter=1;
  I2C.state=I2CChipState_TransmittingToMaster;

  /* Get the value that we are about to send */
  I2C.DataBuffer=I2C.Data[I2C.WordAddress]; /* Must do specials for clock/control regs */

  if (I2C.WordAddress==1) {
    I2C.Data[1]++; /* OK - should do BCD! */
  };
  
  /* Now put the first byte onto the data line */
  I2CDATAWRITE(((I2C.DataBuffer & 128)!=0));
  I2C.DataBuffer<<=1;
  I2C.NumberOfBitsSoFar=1;
}; /* I2C_SetupTransmit */

/*------------------------------------------------------------------------------*/
static void SaveCMOS(ARMul_State *state) {
  int loop,dest;
  unsigned char val;
  FILE *OutFile;

#ifdef __riscos__
  OutFile = fopen("<ArcEm$Dir>.hexcmos", "w");
  if (OutFile == NULL) {
    fprintf(stderr,"SaveCMOS: Could not open CMOS file for writing\n");
    return;
  }
#else
  OutFile = fopen("hexcmos.updated","w");
  if (OutFile == NULL) {
    fprintf(stderr,"SaveCMOS: Could not open (hexcmos.updated) CMOS settings file\n");
    exit(1);
  };
#endif

  for (loop = 0; loop < 240; loop++) {
    dest = loop + 64;
    if (dest > 255) dest -= 240;
    val = ioc.i2c.Data[dest];
    fprintf(OutFile,"%x\n",val);
  };

  fclose(OutFile);
}; /* SaveCMOS */

/*------------------------------------------------------------------------------*/
/* Called when the I2C clock changes state                                      */
void I2C_Update(ARMul_State *state) {
  int NewClockState=I2CCLOCKREAD;
  int ClockEdge;

  ClockEdge=NewClockState!=I2C.OldClockState;

  /*fprintf(stderr,"I2C_Update: current state=%s Clock=%d Data=%d BitsSoFar=%d DataBuffer=%d\n",
    I2CStateNameTrans[I2C.state],I2CCLOCKREAD,I2CDATAREAD,I2C.NumberOfBitsSoFar,I2C.DataBuffer); */

  if ((ClockEdge) && (I2CCLOCKREAD)) {
    /* Rising edge of the clock - store the data state */
    I2C.OldDataState=I2CDATAREAD;
  };

  if ((!ClockEdge) && (I2CCLOCKREAD)) {
    /* High clock, and not a clock edge - possibly a start/stop event */
    if (I2C.OldDataState!=I2CDATAREAD) {
      /* Yep - its a start or a stop - lets find out which! */
      if (I2C.OldDataState!=0) {
        /* Its a start */
#ifdef DEBUG_I2C    
        fprintf(stderr,"I2C Start!\n");
#endif    
        I2C.state=I2CChipState_GotStart;
        I2C.IAmTransmitter=0;
        I2CDATAWRITE(1);
        I2C.NumberOfBitsSoFar=0;
        I2C.DataBuffer=0;

        I2C.OldDataState=I2CDATAREAD;

        return;
      } else {
        /* Its a stop */
#ifdef DEBUG_I2C    
        fprintf(stderr,"I2C Stop!\n");
#endif    
        I2C.state=I2CChipState_Idle;
        I2C.IAmTransmitter=0;
        I2CDATAWRITE(1);
        I2C.OldDataState=I2CDATAREAD;
        return;
      }; /* Is it a start or a stop? */
    }; /* Data changed */
  }; /* Called while clock is high */

 if (!ClockEdge) return;

 I2C.OldClockState=NewClockState;

 /* Must be an edge now */
  if (!(I2C.IAmTransmitter)) {
    if (!I2CCLOCKREAD) {
       /* Normal incoming data - we're at the falling edge */
       switch (I2C.state) {
             /*-----------------------------------------------*/
        case I2CChipState_GotStart:
          /* Used so we don't read the data from the edge after the
             start condition */
          I2C.state=I2CChipState_WaitingForSlaveAddr;
          break;
             /*-----------------------------------------------*/
        case I2CChipState_WaitingForSlaveAddr:
          I2C.DataBuffer*=2;
          I2C.DataBuffer|=I2CDATAREAD;
          I2C.NumberOfBitsSoFar++;
          if (I2C.NumberOfBitsSoFar==8) {
            /* Ah - we have got the whole of the slave address - well except for the last
               bit we are going to ignore it; the last bit tells us rNw */
            I2C.LastrNw=I2C.DataBuffer & 1;
#ifdef DEBUG_I2C      
            fprintf(stderr,"I2C simulator got slave address 0x%x\n",I2C.DataBuffer);
#endif      
            if ((I2C.DataBuffer & 0xfe)!=0xa0) {
              /* Hey - its a request for a different I2C peripheral - like an A500's timer
                 chip */
              fprintf(stderr,"I2C simulator got wierd slave address 0x%x\n",I2C.DataBuffer);
              I2C.IAmTransmitter=0;
              I2C.NumberOfBitsSoFar=0;
              I2C.state=I2CChipState_Idle;
            } else {
              /* Good - its for us */
              I2C.IAmTransmitter=1;
              I2C.NumberOfBitsSoFar=0;
              I2C.state=I2CChipState_AckAfterSlaveAddr;
              I2CDATAWRITE(0);
            };
          };
          break;
             /*-----------------------------------------------*/
        case I2CChipState_GettingWordAddr:
          I2C.DataBuffer*=2;
          I2C.DataBuffer|=I2CDATAREAD;
          I2C.NumberOfBitsSoFar++;
          if (I2C.NumberOfBitsSoFar==8) {
            /* Got the whole word address */
            I2C.WordAddress=I2C.DataBuffer;
            I2C.DataBuffer=0;
            I2C.IAmTransmitter=1; /* Acknowledge */
            I2C.NumberOfBitsSoFar=0;
            I2C.state=I2CChipState_AckAfterWordAddr;
            I2CDATAWRITE(0);
#ifdef DEBUG_I2C      
            fprintf(stderr,"I2C simulator got word address 0x%x (=%d -64=%d) (pres content=0x%x)\n",I2C.WordAddress,
                           I2C.WordAddress,I2C.WordAddress-64,I2C.Data[I2C.WordAddress]);
#endif      
          };
          break;

        case I2CChipState_GettingData:
          I2C.DataBuffer*=2;
          I2C.DataBuffer|=I2CDATAREAD;
          I2C.NumberOfBitsSoFar++;
          if (I2C.NumberOfBitsSoFar==8) {
            /* Got the data */
            I2C.Data[I2C.WordAddress]=I2C.DataBuffer;
#ifdef DEBUG_I2C      
            fprintf(stderr,"I2C simulator got write into address %d value=%d\n",I2C.WordAddress,
                    I2C.DataBuffer);
#endif      
            SaveCMOS(state); /* RY */
            I2C.WordAddress++; /* Auto inc address */
            I2C.DataBuffer=0;
            I2C.NumberOfBitsSoFar=0;
            /* Acknowledge */
            I2C.IAmTransmitter=1;
            I2C.state=I2CChipState_AckAfterReceiveData;
            I2CDATAWRITE(0);
          };
          break;

        case I2CChipState_AckAfterTransmitData:
          /* OK - I reckon we are wondering if the transmitter is going to send any more -
             the data book seems to imply an extra bit in there - p.287 - don't belive it ! */
          if (I2CDATAREAD) {
            /* The data line is high - that means that there was no acknowledge to our data
               - so it doesn't want any more */
            I2C.IAmTransmitter=0;
            I2CDATAWRITE(1);
            I2C.DataBuffer=0;
            I2C.state=I2CChipState_Idle; /* Actually waiting for a stop - perhaps we should have a separate state */
          } else {
            /* It acknowledged - that means it wants more */
            I2C_SetupTransmit(state);
          };
          break;

        default:
          /* Complain - weren't expecting to get here? */
          break;
      }; /* State switch */
    }; /* Falling edge */
  } else {
    /* I am the transmitter - I might have to do something here */
    /* Think we can ignore rising edge - what ever we were supposed to do, we should have done
    in the low area (if we were a master we would need rising to let us send a stop or start */
    if (!I2CCLOCKREAD) {
      switch (I2C.state) {
               /*-----------------------------------------------*/
        case I2CChipState_AckAfterSlaveAddr:
          /* If LastrNW indicates a write then we are going to receive the word address,
             if its a read then we go and transmit some data to the master */
          if (I2C.LastrNw) {
            /* Read */
            I2C_SetupTransmit(state);
          } else {
            /* Write */
            I2C.state=I2CChipState_GettingWordAddr; /* Writing the word address */
            I2C.NumberOfBitsSoFar=0;
            I2C.DataBuffer=0;
            I2C.IAmTransmitter=0;
            I2CDATAWRITE(1);
          };
          break;

               /*-----------------------------------------------*/
        case I2CChipState_AckAfterWordAddr:
          /* If LastrNw indicates a write then the host is about to send some data
             for us to write, else host is reading some data */
          if (I2C.LastrNw) {
            /* Read */
            I2C.state=I2CChipState_TransmittingToMaster; /* i.e. its reading */
            I2C.NumberOfBitsSoFar=0;
            I2C.DataBuffer=0xaa; /* Should set this up - as above! */
          } else {
            /* Write */
            I2C.state=I2CChipState_GettingData; /* Writing the data from the master */
            I2C.NumberOfBitsSoFar=0;
            I2C.DataBuffer=0;
            I2C.IAmTransmitter=0;
            I2CDATAWRITE(1);
          };
          break;

               /*-----------------------------------------------*/
        case I2CChipState_AckAfterReceiveData:
          /* OK - we just sent an ack for some data we received */
          /* We are going to stay in the receive data to allow us to get some more data */
          /* Write */
          I2C.state=I2CChipState_GettingData; /* Writing the data from the master */
          I2C.NumberOfBitsSoFar=0;
          I2C.DataBuffer=0;
          I2C.IAmTransmitter=0;
          I2CDATAWRITE(1);
          break;
               /*-----------------------------------------------*/
        case I2CChipState_TransmittingToMaster:
          /* Send the next data bit */
          I2CDATAWRITE(((I2C.DataBuffer & 128)!=0));
          I2C.DataBuffer<<=1;
          I2C.NumberOfBitsSoFar++;

          if (I2C.NumberOfBitsSoFar==9) {
            /* We've sent that byte - besides incrementing the address
               we have to figure out if the master wants to get another byte - if it does it will acknowledge
               correctly */
#ifdef DEBUG_I2C       
            fprintf(stderr,"I2C simulator - just finished sending data byte at address 0x%x\n",I2C.WordAddress);
#endif      
            I2C.WordAddress++;
            I2C.DataBuffer=0;
            I2C.state=I2CChipState_AckAfterTransmitData;
            I2C.IAmTransmitter=0; /* No - we want the other guy to acknowledge our data...I think! */
            I2CDATAWRITE(1);
          };
          break;
     
        default:
          /* Complain - weren't expecting to be here in this state */
          break;
      }; /* state for our sending */
    }; /* Clock edge */
  }; /* Transmitter */
}; /* I2C_Update */

/* ------------------------------------------------------------------------- */
static void SetUpCMOS(ARMul_State *state) {
  int loop,dest;
  unsigned int val;
  FILE *InFile;

#ifdef __riscos__
  InFile = fopen("<ArcEm$Dir>.hexcmos", "r");
#else
  InFile = fopen("hexcmos", "r");
#endif
  if (InFile == NULL) {
    fprintf(stderr,"SetUpCMOS: Could not open (hexcmos) CMOS settings file\n");
    exit(1);
  };

  for (loop = 0; loop < 240; loop++) {
    fscanf(InFile,"%x\n",&val);

    dest=loop+64;
    if (dest>255) dest-=240;

    ioc.i2c.Data[dest]=val;
  };

  fclose(InFile);
}; /* SetUpCMOS */

/* ------------------------------------------------------------------------- */
void I2C_Init(ARMul_State *state) {
 I2C.OldDataState=0;
 I2C.OldClockState=0;
 I2C.IAmTransmitter=0;
 I2CDATAWRITE(1);
 I2C.state=I2CChipState_Idle;
 I2C.NumberOfBitsSoFar=0;
 I2C.LastrNw=0;
 I2C.WordAddress=0;

 SetUpCMOS(state);
}; /* I2C_Init */
