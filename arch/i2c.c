/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
/* SaveCMOS contributed by Richard York */
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include "../armdefs.h"

#ifdef MACOSX
#include <unistd.h>
extern char arcemDir[256];
#endif

#include "armarc.h"
#include "i2c.h"

#include "dbugsys.h"
#include "ControlPane.h"

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

/* Macros taken from bcd.h in the Linux kernel - licensed under GPL2+ */
#define BCD2BIN(val)    (((val) & 0x0f) + ((val)>>4)*10)
#define BIN2BCD(val)    ((((val)/10)<<4) + (val)%10)


typedef enum {
  I2CChipState_Idle=0,
  I2CChipState_GotStart, /* Got a special condition */
  I2CChipState_WaitingForSlaveAddr,
  I2CChipState_AckAfterSlaveAddr,
  I2CChipState_GettingWordAddr,
  I2CChipState_AckAfterWordAddr,
  I2CChipState_TransmittingToMaster,
  I2CChipState_AckAfterTransmitData,
  I2CChipState_GettingData,
  I2CChipState_AckAfterReceiveData
} I2CState;

struct I2CStruct {
  uint8_t Data[256];
  bool IAmTransmitter;
  uint8_t OldDataState;
  uint8_t OldClockState;
  I2CState state;
  uint_fast16_t DataBuffer;
  uint8_t NumberOfBitsSoFar;
  bool LastrNw;
  uint8_t WordAddress; /* Note unsigned char - can never be outside Data bounds */
};

static struct I2CStruct I2C;

/* These are "sensible" defaults from the CVS hexcmos file,
   as opposed to "factory" defaults, which are not quite as sensible */

unsigned char CMOSDefaults[] = {
	0x0,0xfe,0x0,0xeb,0x0,0x8,0x0,0x0,0x0,0x0,
	0x10,0x50,0x1d,0xc,0x0,0x2e,0x80,0x2,0x0,0x0,
	0x0,0x0,0x3,0xa,0x0,0x1,0x0,0x0,0x2,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x0,0x0,0x1,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x60,0x13,
	0x0,0x0,0x4,0x91,0x20,0x8,0xf3,0x0,0x0,0x0,
	0xff,0x0,0x0,0xa,0x0,0x0,0x0,0x0,0xf0,0xa8,
	0xc3,0x0,0xcf,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x38,0x0,
	0x1,0x2e,0x7c,0x7b,0x7d,0xa,0x50,0x0,0x80,0x2,
	0x0,0x0,0x0,0x0,0x3,0x0,0x1b,0x6f,0x40,0x20,
	0x80,0x15,0xf3,0x8e,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xd,0x0,0x0,0xf,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x69
};

/*------------------------------------------------------------------------------*/
static void
I2C_SetupTransmit(ARMul_State *state)
{
  /*fprintf(stderr,"I2C_SetupTransmit (address=%d)\n",I2C.WordAddress); */
  I2C.IAmTransmitter = true;
  I2C.state = I2CChipState_TransmittingToMaster;

  /* Get the value that we are about to send */
  if (I2C.WordAddress < 16) {
    /* I2C Addresses in the range 0..15 are Clock/Control Registers.
       Generate these values dynamically rather than referring to the
       I2C.Data array
    */
    time_t t1 = time(NULL);
    const struct tm *t2 = gmtime(&t1);

    switch (I2C.WordAddress) {
    case 2: /* Seconds */
      I2C.DataBuffer = BIN2BCD(t2->tm_sec);
      break;
    case 3: /* Minutes */
      I2C.DataBuffer = BIN2BCD(t2->tm_min);
      break;
    case 4: /* Hours */
      I2C.DataBuffer = BIN2BCD(t2->tm_hour);
      break;
    case 5: /* Year/Date */

      I2C.DataBuffer = (((t2->tm_year + 1900) & 3) << 6) |
                       BIN2BCD(t2->tm_mday);

      break;
    case 6: /* Weekday/Month */
      I2C.DataBuffer = (t2->tm_wday << 5) |
                       BIN2BCD(t2->tm_mon + 1);
      break;
    default:
      I2C.DataBuffer = 0; /* Just to avoid being undefined */
      break;
    }
  } else {
    /* I2C Addreses in the range 16..255 are NVRAM */
    I2C.DataBuffer = I2C.Data[I2C.WordAddress];
  }

  /* I have no idea why these next three lines are here:
     When WordAddress==1 this refers to the centisecond counter of the RTC.
     This code increments the data location after it is read.
     However it is not called every centisecond - and therefore provides
     no useful timing.
  */
  if (I2C.WordAddress == 1) {
    I2C.Data[1]++; /* OK - should do BCD! */
  }

  /* Now put the first byte onto the data line */
  I2CDATAWRITE(((I2C.DataBuffer & 128) != 0));
  I2C.DataBuffer <<= 1;
  I2C.NumberOfBitsSoFar = 1;
} /* I2C_SetupTransmit */


/*----------------------------------------------------------------------------*/
static void SaveCMOS(ARMul_State *state) {
  int loop,dest;
  unsigned char val;
  FILE *OutFile;
  const char *filename;

#ifdef __riscos__
  filename = "<ArcEm$Dir>.hexcmos";
#else
  filename = "hexcmos";
#endif

#ifdef MACOSX
  chdir(arcemDir);
#endif

  OutFile = fopen(filename,"w");
  if (OutFile == NULL) {
    ControlPane_Error(1,"SaveCMOS: Could not open CMOS settings file (%s)\n", filename);
  };

  for (loop = 0; loop < 240; loop++) {
    dest = loop + 64;
    if (dest > 255) dest -= 240;
    val = I2C.Data[dest];
    fprintf(OutFile,"%x\n",val);
  };

  fclose(OutFile);
} /* SaveCMOS */

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
        dbug_i2c("I2C Start!\n");
        I2C.state=I2CChipState_GotStart;
        I2C.IAmTransmitter=false;
        I2CDATAWRITE(1);
        I2C.NumberOfBitsSoFar=0;
        I2C.DataBuffer=0;

        I2C.OldDataState=I2CDATAREAD;

        return;
      } else {
        /* Its a stop */
        dbug_i2c("I2C Stop!\n");
        I2C.state=I2CChipState_Idle;
        I2C.IAmTransmitter=false;
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
            dbug_i2c("I2C simulator got slave address "
                "%d\n", (int) I2C.DataBuffer);
            if ((I2C.DataBuffer & 0xfe)!=0xa0) {
              /* Hey - its a request for a different I2C peripheral - like an A500's timer
                 chip */
              fprintf(stderr, "I2C simulator got wierd slave address "
                  "%d\n", (int) I2C.DataBuffer);
              I2C.IAmTransmitter=false;
              I2C.NumberOfBitsSoFar=0;
              I2C.state=I2CChipState_Idle;
            } else {
              /* Good - its for us */
              I2C.IAmTransmitter=true;
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
            I2C.IAmTransmitter=true; /* Acknowledge */
            I2C.NumberOfBitsSoFar=0;
            I2C.state=I2CChipState_AckAfterWordAddr;
            I2CDATAWRITE(0);
            dbug_i2c("I2C simulator got word address 0x%x (=%d -64=%d) (pres content=0x%x)\n",I2C.WordAddress,
                           I2C.WordAddress,I2C.WordAddress-64,I2C.Data[I2C.WordAddress]);
          };
          break;

        case I2CChipState_GettingData:
          I2C.DataBuffer*=2;
          I2C.DataBuffer|=I2CDATAREAD;
          I2C.NumberOfBitsSoFar++;
          if (I2C.NumberOfBitsSoFar==8) {
            /* Got the data */
            I2C.Data[I2C.WordAddress]=I2C.DataBuffer;
            dbug_i2c("I2C simulator got write into address %d value=%d\n",I2C.WordAddress,
                    I2C.DataBuffer);
            SaveCMOS(state); /* RY */
            I2C.WordAddress++; /* Auto inc address */
            I2C.DataBuffer=0;
            I2C.NumberOfBitsSoFar=0;
            /* Acknowledge */
            I2C.IAmTransmitter=true;
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
            I2C.IAmTransmitter=false;
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
            I2C.IAmTransmitter=false;
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
            I2C.IAmTransmitter=false;
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
          I2C.IAmTransmitter=false;
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
            dbug_i2c("I2C simulator - just finished sending data byte at address 0x%x\n",I2C.WordAddress);
            I2C.WordAddress++;
            I2C.DataBuffer=0;
            I2C.state=I2CChipState_AckAfterTransmitData;
            I2C.IAmTransmitter=false; /* No - we want the other guy to acknowledge our data...I think! */
            I2CDATAWRITE(1);
          };
          break;

        default:
          /* Complain - weren't expecting to be here in this state */
          break;
      }; /* state for our sending */
    }; /* Clock edge */
  }; /* Transmitter */
} /* I2C_Update */

/* ------------------------------------------------------------------ */

static void SetUpCMOS(ARMul_State *state)
{
    const char *path;
    FILE *fp;
    int byte, dest;
    unsigned int val;

#if defined(__riscos__)
    path = "<ArcEm$Dir>.hexcmos";
#elif defined(MACOSX)
    chdir(arcemDir);
    path = "hexcmos";
#else
    path = "hexcmos";
#endif

    if ((fp = fopen(path, "r")) == NULL)
        fputs("SetUpCMOS: Could not open (hexcmos) CMOS settings file, "
            "resetting to internal defaults.\n", stderr);

    for (byte = 0; byte < 240; byte++) {
        if (fp) {
            if (fscanf(fp, "%x\n", &val) != 1) {
                ControlPane_Error(1,"arcem: failed to read CMOS value %d of %s\n",
                    byte, path);
            }
        } else {
            val = CMOSDefaults[byte];
        }

        /* Map 0..191 to 64...255, and 192...239 to 16...63. */
        dest = byte + 64;
        if (dest > 255) dest -= 240;
        I2C.Data[dest] = val;
    }

    if (fp) fclose(fp);
}

/* ------------------------------------------------------------------ */

void
I2C_Init(ARMul_State *state)
{
  I2C.OldDataState = 0;
  I2C.OldClockState = 0;
  I2C.IAmTransmitter = false;
  I2CDATAWRITE(1);
  I2C.state = I2CChipState_Idle;
  I2C.NumberOfBitsSoFar = 0;
  I2C.LastrNw = false;
  I2C.WordAddress = 0;

  SetUpCMOS(state);
} /* I2C_Init */
