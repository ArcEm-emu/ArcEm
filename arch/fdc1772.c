/* Emulation of 1772 fdc */
/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */

/*#define DEBUG_FDC */

#include <stdlib.h>

#define __USE_FIXED_PROTOTYPES__
#include <errno.h>
#include <stdio.h>
//#include <unistd.h>

#include "../armopts.h"
#include "../armdefs.h"

#include "armarc.h"
#include "ControlPane.h"
#include "fdc1772.h"

#define READSPACING 1
#define WRITESPACING 1
#define READADDRSTART 250
#define SEEKDELAY 1

#define BIT_BUSY 1
#define BIT_DRQ (1<<1)
#define BIT_TR00 (1<<2)
#define BIT_LOSTDATA (1<<2)
#define BIT_CRC (1<<3)
#define BIT_RECNOTFOUND (1<<4)
#define BIT_MOTORSPINUP (1<<5)
#define BIT_MOTORON (1<<7)
#define TYPE1_UPDATETRACK (1<<4)
#define TYPE2_BIT_MOTORON (1<<2)
#define TYPE2_BIT_MULTISECTOR (1<<4)

typedef struct {
    char *name;
    int bytes_per_sector;
    int sector_size_code;
    int sectors_per_track;
    int sector_base;
    int num_tracks;
} floppy_format;

static floppy_format avail_format[] = {
    { "ADFS 800KB", 1024, 3, 5, 0, 80 },
    { "DOS 720KB", 512, 2, 9, 1, 80 },
};

/* points to an element of avail_format. */
static floppy_format *format;

/* give the byte offset of a given sector. */
#define SECTOR_LOC_TO_BYTE_OFF(track, side, sector) \
    (((track * 2 + side) * format->sectors_per_track + \
        (sector - format->sector_base)) * format->bytes_per_sector)

static void FDC_DoWriteChar(ARMul_State *state);
static void FDC_DoReadChar(ARMul_State *state);
static void FDC_DoReadAddressChar(ARMul_State *state);

/*--------------------------------------------------------------------------*/
static void GenInterrupt(ARMul_State *state, const char *reason) {
#ifdef DEBUG_FDC 
  fprintf(stderr,"FDC:GenInterrupt: %s\n",reason);
#endif 
  ioc.FIRQStatus|=2; /* FH1 line on IOC */
#ifdef DEBUG_FDC
  fprintf(stderr,"FDC:GenInterrupt FIRQStatus=0x%x Mask=0x%x\n",
    ioc.FIRQStatus,ioc.FIRQMask);
#endif
  IO_UpdateNfiq();
}; /* GenInterrupt */


/*--------------------------------------------------------------------------*/
unsigned int FDC_Regular(ARMul_State *state) {
  int ActualTrack;

  if (--FDC.DelayCount) return 0;

  switch (FDC.LastCommand & 0xf0) {
    case 0x0:
      /* Restore command */
      FDC.StatusReg|=BIT_MOTORON | BIT_MOTORSPINUP | BIT_TR00;
      FDC.Track=0;
      GenInterrupt(state,"Restore complete");
      FDC.LastCommand=0xd0;
      FDC.StatusReg&=~BIT_BUSY;
      break;

    case 0x10:
      /* Seek command - it will complete now */
      FDC.LastCommand=0xd0;
      FDC.StatusReg&=~BIT_BUSY;
      FDC.StatusReg|=BIT_MOTORSPINUP|BIT_MOTORON;
      FDC.Track=FDC.Data; /* Got to the desired track */
      if (FDC.Track==0) FDC.StatusReg|=BIT_TR00;
      GenInterrupt(state,"Seek complete");
      break;

    case 0x20:
    case 0x30:
    case 0x40:
    case 0x50:
    case 0x60:
    case 0x70:
      /* Step in/out/same commands - it will complete now */
      FDC.StatusReg&=~BIT_BUSY;
      FDC.StatusReg|=BIT_MOTORSPINUP|BIT_MOTORON;
      ActualTrack=FDC.Track+FDC.Direction;
      if (FDC.LastCommand & TYPE1_UPDATETRACK)
        FDC.Track=ActualTrack; /* Got to the desired track */
      if (ActualTrack==0) FDC.StatusReg|=BIT_TR00;
      FDC.LastCommand=0xd0;
      GenInterrupt(state,"Step complete");
      break;

    case 0x80:
    case 0x90:
      /* Read sector next character */
      if (FDC.BytesToGo) FDC_DoReadChar(state);
      FDC.DelayCount=FDC.DelayLatch;
      break;

    case 0xa0:
    case 0xb0:
      /* Write sector next character */
      FDC_DoWriteChar(state);
      FDC.DelayCount=FDC.DelayLatch;
      break;

    case 0xc0:
      FDC_DoReadAddressChar(state);
      FDC.DelayCount=FDC.DelayLatch;
      break;

  }; /* FDC_Regular */

  return 0;
}; /* FDC_Regular */

/*--------------------------------------------------------------------------*/
static void ClearInterrupt(ARMul_State *state) {
  ioc.FIRQStatus&=~2; /* FH1 line on IOC */
  IO_UpdateNfiq();
}; /* ClearInterrupt */

/*--------------------------------------------------------------------------*/
static void GenDRQ(ARMul_State *state) {
#ifdef DEBUG_FDC 
  fprintf(stderr,"FDC_GenDRQ (data=0x%x)\n",FDC.Data);
#endif 
  ioc.FIRQStatus|=1; /* FH0 line on IOC */
  IO_UpdateNfiq();
}; /* GenDRQ */

/*--------------------------------------------------------------------------*/
static void ClearDRQ(ARMul_State *state) {
#ifdef DEBUG_FDC 
  fprintf(stderr,"FDC_ClearDRQ\n");
#endif 
  ioc.FIRQStatus&=~1; /* FH0 line on IOC */
  IO_UpdateNfiq();
  FDC.StatusReg&=~BIT_DRQ;
}; /* ClearDRQ */

/*--------------------------------------------------------------------------*/
void FDC_LatchAChange(ARMul_State *state) {
  static unsigned long TimeWhenInUseChanged,now,diff;
  int bit;
  int val;
  int diffmask=ioc.LatchA ^ ioc.LatchAold;

#ifdef DEBUG_FDC
  fprintf(stderr,"LatchA: 0x%x\n",ioc.LatchA);
#endif

  /* Start up test */
  if (ioc.LatchAold==-1) diffmask=0xff;

  for(bit=7;bit>=0;bit--) {
    if (diffmask & (1<<bit)) {
      /* Bit changed! */
      val = ioc.LatchA & (1 << bit);

      switch (bit) {
        case 0:
        case 1:
        case 2:
        case 3:
#ifdef DEBUG_FDC
          fprintf(stderr,"Floppy drive select %d gone %s\n",bit,val?"High":"Low");
#endif
          if (!val)
            FDC.CurrentDisc=bit;
          break;

        case 4:
#ifdef DEBUG_FDC
          fprintf(stderr,"Floppy drive side select now %d\n",val?1:0);
#endif
          break;

        case 5:
#ifdef DEBUG_FDC
          fprintf(stderr,"Floppy drive Motor now %s\n",val?"On":"Off");
#endif          
          break;

        case 6:
          now=ARMul_Time;
          diff=now-TimeWhenInUseChanged;
#ifdef DEBUG_FDC
          fprintf(stderr,"Floppy In use line now %d (was %s for %lu ticks)\n",
                  val?1:0,val?"low":"high",diff);
#endif
          TimeWhenInUseChanged=now;
          break;

        case 7:
#ifdef DEBUG_FDC
          fprintf(stderr,"Floppy eject/disc change reset now %d\n",val?1:0);
#endif
          break;
      }; /* Bit changed switch */
    }; /* Difference if */
  }; /* bit loop */

  /* Redraw floppy LEDs if necessary */
  if (diffmask & 0xf)
    ControlPane_RedrawLeds(state);
}; /* FDC_LatchAChange */

/*--------------------------------------------------------------------------*/
void FDC_LatchBChange(ARMul_State *state) {
  int bit;
  int val;
  int diffmask=ioc.LatchB ^ ioc.LatchBold;

#ifdef DEBUG_FDC
  fprintf(stderr,"LatchB: 0x%x\n",ioc.LatchB);
#endif
  /* Start up test */
  if (ioc.LatchBold==-1) diffmask=0xff;

  for(bit=7;bit>=0;bit--) {
    if (diffmask & (1<<bit)) {
      /* Bit changed! */
      val=ioc.LatchB & (1<<bit);

#ifdef DEBUG_FDC
      switch (bit) {
        case 0:
        case 2:
          fprintf(stderr,"Latch B bit %d now %d\n",bit,val?1:0);
          break;

        case 1:
          fprintf(stderr,"FDC format now %s Density\n",val?"Single":"Double");
          break;

        case 3:
          fprintf(stderr,"Floppy drive reset now %s\n",val?"High":"Low");
          break;

        case 4:
          fprintf(stderr,"Printer strobe now %d\n",val?1:0);
          break;

        case 5:
          fprintf(stderr,"Aux 1 now %d\n",val?1:0);
          break;

        case 6:
          fprintf(stderr,"Aux 2 now %d\n",val?1:0);
          break;

        case 7:
          fprintf(stderr,"HDC HS3 line now %d\n",val?1:0);
          break;
      }; /* Bit changed switch */
#endif      
    }; /* Difference if */
  }; /* bit loop */
}; /* FDC_LatchBChange */
/*--------------------------------------------------------------------------*/
static void ReadDataRegSpecial(ARMul_State *state) {
  switch (FDC.LastCommand & 0xf0) {
    case 0x80:
    case 0x90:
      /* Read sector - if we just read the last piece of data we can issue
         a completed interrupt */
      if (!FDC.BytesToGo) {
        GenInterrupt(state,"Read end (b)");
        FDC.LastCommand=0xd0; /* Force int with no interrupt */
        FDC.StatusReg&=~BIT_BUSY;
      };
      break;

    case 0xc0:
      /* Read Address - if we just read the last piece of data we can issue
         a completed interrupt */
      if (!FDC.BytesToGo) {
        GenInterrupt(state,"Read addr end");
        FDC.LastCommand=0xd0; /* Force int with no interrupt */
        FDC.StatusReg&=~BIT_BUSY;
        /* Supposed to copy the track into the sector register */
        FDC.Sector=FDC.Track;
      };
      break;

  }; /* Last command type switch */
}; /* ReadDataRegSpecial */

/*--------------------------------------------------------------------------*/
ARMword FDC_Read(ARMul_State *state, ARMword offset) {
  int reg=(offset>>2) &3;

  switch (reg) {
    case 0: /* Status */
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_Read: Status reg=0x%x pc=%08x r[15]=%08x\n",FDC.StatusReg,state->pc,state->Reg[15]);
#endif
      ClearInterrupt(state);
      return(FDC.StatusReg);
      break;

    case 1: /* Track */
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_Read: Track reg\n");
#endif
      return(FDC.Track);
      break;

    case 2: /* Sector */
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_Read: Sector reg\n");
#endif
      return(FDC.Sector);
      break;

    case 3: /* Data */
      /*fprintf(stderr,"FDC_Read: Data reg=0x%x (BytesToGo=%d)\n",FDC.Data,FDC.BytesToGo); */
      ClearDRQ(state);
      ReadDataRegSpecial(state);
      return(FDC.Data);
      break;
  };

  return(0);
}; /* FDC_Read */

/*--------------------------------------------------------------------------*/
static void FDC_DoDRQ(ARMul_State *state) {
  /* If they haven't read the previous data then flag a problem */
  if (FDC.StatusReg & BIT_DRQ) {
    FDC.StatusReg |=BIT_LOSTDATA;
#ifdef DEBUG_FDC
    fprintf(stderr,"FDC: LostData\n");
#endif
  };

  FDC.StatusReg|=BIT_DRQ;
  GenDRQ(state);
}; /* FDC_DoDRQ */

/*--------------------------------------------------------------------------*/
static void FDC_NextTrack(ARMul_State *state) {
  FDC.Track++;
  if (FDC.Track==80) {
    FDC.Track=80;
  };
}; /* FDC_NextTrack */
/*--------------------------------------------------------------------------*/
static void FDC_NextSector(ARMul_State *state) {
  FDC.Sector++;
  if (FDC.Sector == format->sectors_per_track + format->sector_base) {
    FDC.Sector = format->sector_base;
    FDC_NextTrack(state);
  };
}; /* FDC_NextSector */
/*--------------------------------------------------------------------------*/
static void FDC_DoReadAddressChar(ARMul_State *state) {
#ifdef DEBUG_FDC
  fprintf(stderr,"FDC_DoReadAddressChar: BytesToGo=%x\n",FDC.BytesToGo);
#endif

  switch (FDC.BytesToGo) {
    case 6: /* Track addr */
      FDC.Data=FDC.Track;
      break;

    case 5: /* side number */
      FDC.Data=(ioc.LatchA & (1<<4))?1:0; /* I've not inverted this - should I ? */
      break;

    case 4: /* sector addr */
      FDC.Data=FDC.Sector_ReadAddr;
      FDC.Sector_ReadAddr++;
      if (FDC.Sector_ReadAddr >= format->sectors_per_track + format->sector_base)
          FDC.Sector_ReadAddr = format->sector_base;
      break;

    case 3: /* sector length */
      FDC.Data = format->sector_size_code; /* 1K per sector (2=512, 1=256, 0=128 */
      break;

    case 2: /* CRC 1 */
      FDC.Data=0xa5; /* ERR_Programmer_notbothered */
      break;

    case 1: /* CRC 2 */
      FDC.Data=0x5a; /* ERR_Programmer_notbothered */
      break;

    default:
      /* The idea here is that if they are running by polling and aren't actually
         reading all the data we should finish anyway */
      if (FDC.StatusReg & BIT_DRQ) {
        FDC.StatusReg |=BIT_LOSTDATA;
#ifdef DEBUG_FDC
        fprintf(stderr,"FDC: LostData (Read address end)\n");
#endif
      };
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC: ReadAddressChar: Terminating command at end\n");
#endif
      GenInterrupt(state,"Read end");
      FDC.LastCommand=0xd0; /* Force int with no interrupt */
      FDC.StatusReg&=~BIT_BUSY;
      /* Supposed to copy the track into the sector register */
      FDC.Sector=FDC.Track;
      break;
  }; /* Bytes left */

  FDC_DoDRQ(state);
  FDC.BytesToGo--;
}; /* FDC_DoReadAddressChar */

/*--------------------------------------------------------------------------*/
static void FDC_DoReadChar(ARMul_State *state) {
  int data;
  if (FDC.FloppyFile[FDC.CurrentDisc]==NULL) {
    data=42;
  } else {
    data=fgetc(FDC.FloppyFile[FDC.CurrentDisc]);
#ifdef DEBUG_FDC
    if (data==EOF) {
      fprintf(stderr,"FDC_DoReadChar: got EOF\n");
    };
#endif
  };
  FDC.Data=data;
  FDC_DoDRQ(state);
  FDC.BytesToGo--;
  if (!FDC.BytesToGo) {
    if (FDC.LastCommand & TYPE2_BIT_MULTISECTOR) {
      FDC_NextSector(state);
      FDC.BytesToGo = format->bytes_per_sector;
    } else {
      /* We'll actually terminate on next data read */
    }; /* Not multisector */
  };
}; /* FDC_DoReadChar */

/*--------------------------------------------------------------------------*/
/* Type 1 command, h/V/r1,r0 - desired track in data reg                    */
static void FDC_SeekCommand(ARMul_State *state) {
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_CRC|BIT_RECNOTFOUND|BIT_TR00);

  ClearInterrupt(state);
  ClearDRQ(state);

  if (FDC.Data >= format->num_tracks) {
    /* Fail!! */
    FDC.StatusReg|=BIT_RECNOTFOUND;
    GenInterrupt(state,"Seek fail");
    FDC.LastCommand=0xd0;
    FDC.StatusReg&=~BIT_BUSY;
  };
  FDC.DelayCount=FDC.DelayLatch=SEEKDELAY;
}; /* FDC_SeekCommand */

/*--------------------------------------------------------------------------*/
/* Type 1 command, u/h/V/r1,r0 -                                            */
static void FDC_StepDirCommand(ARMul_State *state,int Dir) {
  int DesiredTrack=FDC.Track+Dir;
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_CRC|BIT_RECNOTFOUND|BIT_TR00);

  ClearInterrupt(state);
  ClearDRQ(state);

  FDC.Direction=Dir;

  if (DesiredTrack >= format->num_tracks || DesiredTrack < 0) {
    /* Fail!! */
    FDC.StatusReg|=BIT_RECNOTFOUND;
    GenInterrupt(state,"Step fail");
    FDC.LastCommand=0xd0;
    FDC.StatusReg&=~BIT_BUSY;
  };
  FDC.DelayCount=FDC.DelayLatch=SEEKDELAY;
}; /* FDC_StepDirCommand */

/*--------------------------------------------------------------------------*/
static void FDC_ReadAddressCommand(ARMul_State *state) {
  unsigned long offset;
  int Side=(ioc.LatchA & (1<<4))?0:1; /* Note: Inverted??? Yep!!! */
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_DRQ | BIT_LOSTDATA | (1<<5) | (1<<6) | BIT_RECNOTFOUND);

    offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);

  if (FDC.FloppyFile[FDC.CurrentDisc]!=NULL) {
    fseek(FDC.FloppyFile[FDC.CurrentDisc],offset,SEEK_SET);

    FDC.BytesToGo=6; /* 6 bytes of data from a Read address command */
    FDC_DoReadAddressChar(state);
    /* This start time must be sufficient so that the OS has time to get out of
       its interrupt handlers etc. and get ready for the next one.
       Its also got to be small enough to allow a read sector of every sector
       on a track in 21cs.  */
    FDC.DelayCount=READADDRSTART;
    FDC.DelayLatch=READSPACING;
  } else {
    FDC.StatusReg|=BIT_RECNOTFOUND;
    GenInterrupt(state,"Couldnt read disc file in ReadAddress");
    FDC.LastCommand=0xd0;
    FDC.StatusReg&=~BIT_BUSY;
  };
}; /* ReadAddressCommand */

/*--------------------------------------------------------------------------*/
static void FDC_ReadCommand(ARMul_State *state) {
  unsigned long offset;
  int Side=(ioc.LatchA & (1<<4))?0:1; /* Note: Inverted??? Yep!!! */

  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_DRQ | BIT_LOSTDATA | (1<<5) | (1<<6) | BIT_RECNOTFOUND);

#ifdef DEBUG_FDC
  fprintf(stderr,"FDC_ReadCommand: Starting with Side=%d Track=%d Sector=%d (CurrentDisc=%d)\n",
                 Side,FDC.Track,FDC.Sector,FDC.CurrentDisc);
#endif

    offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);

  if (FDC.FloppyFile[FDC.CurrentDisc]!=NULL) {
    if (fseek(FDC.FloppyFile[FDC.CurrentDisc],offset,SEEK_SET)!=0) {
      fprintf(stderr,"FDC_ReadCommand: fseek failed!\n");
    };
  };

    FDC.BytesToGo = format->bytes_per_sector;
  /* FDC_DoReadChar(state); - let the regular code do this */
  FDC.DelayCount=FDC.DelayLatch=READSPACING;
}; /* FDC_ReadCommand */

/*--------------------------------------------------------------------------*/
/* Called on the regular event if a write data command is in progress
   The BytesToGo has a special state.  If its greater than the sector size it
   means we have not yet generated the initial DRQ */
/* Essentially all this routine has to do is provide the DRQ's - the
   actual write is done when the data register is written to */
static void FDC_DoWriteChar(ARMul_State *state) {
  if (FDC.BytesToGo > format->bytes_per_sector) {
    /* Initial DRQ */
    FDC_DoDRQ(state);
    FDC.BytesToGo = format->bytes_per_sector;
    return;
  };

  if (FDC.BytesToGo!=0) {
    FDC_DoDRQ(state);
    return;
  };

  /* OK - this is the final case - end of the sector */
  /* but if its a multi sector command then we just have to carry on */
  if (FDC.LastCommand & TYPE2_BIT_MULTISECTOR) {
     FDC_NextSector(state);
     FDC.BytesToGo = format->bytes_per_sector;
     FDC_DoDRQ(state);
  } else {
    /* really the end */
    GenInterrupt(state,"end write");
    FDC.LastCommand=0xd0; /* Force int with no interrupt */
    FDC.StatusReg&=~BIT_BUSY;
    ClearDRQ(state);
  };
}; /* FDC_DoWriteChar */
/*--------------------------------------------------------------------------*/
static void FDC_WriteCommand(ARMul_State *state) {
  unsigned long offset;
  int Side=(ioc.LatchA & (1<<4))?0:1; /* Note: Inverted??? Yep!!! */
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_DRQ | BIT_LOSTDATA | (1<<5) | (1<<6) | BIT_RECNOTFOUND);

#ifdef DEBUG_FDC
  fprintf(stderr,"FDC_WriteCommand: Starting with Side=%d Track=%d Sector=%d\n",
                 Side,FDC.Track,FDC.Sector);
#endif

    offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);
  fseek(FDC.FloppyFile[FDC.CurrentDisc],offset,SEEK_SET);

  FDC.BytesToGo = format->bytes_per_sector + 1;
  /*GenDRQ(state); */ /* Please mister host - give me some data! - no that should happen on the regular!*/
  FDC.DelayCount=FDC.DelayLatch=WRITESPACING;
}; /* FDC_WriteCommand */
/*--------------------------------------------------------------------------*/
static void FDC_RestoreCommand(ARMul_State *state) {
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_RECNOTFOUND | BIT_CRC | BIT_DRQ);
  FDC.DelayCount=FDC.DelayLatch=READSPACING;
}; /* FDC_RestoreCommand */
/*--------------------------------------------------------------------------*/
static void FDC_NewCommand(ARMul_State *state, ARMword data) {
  ClearInterrupt(state);
  switch (data & 0xf0) {
    case 0x00: /* Restore */
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Restore data=0x%x pc=%08x r[15]=%08x\n",
              data,state->pc,state->Reg[15]);
#endif
      FDC.LastCommand=data;
      FDC_RestoreCommand(state);
      break;

    case 0x10: /* Seek */
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Seek data=0x%x\n",data);
#endif
      FDC.LastCommand=data;
      FDC_SeekCommand(state);
      break;

    case 0x20:
    case 0x30:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Step data=0x%x\n",data);
#endif
      FDC.LastCommand=data;
      FDC_StepDirCommand(state,FDC.Direction);
      break;

    case 0x40:
    case 0x50:
#ifdef DEBUG_FDC
    fprintf(stderr,"FDC_NewCommand: Step in data=0x%x\n",data);
#endif
    FDC.LastCommand=data;
    /* !!!! NOTE StepIn means move towards the centre of the disc - i.e.*/
    /*           increase track!!!                                      */
      FDC_StepDirCommand(state,+1);
      break;

    case 0x60:
    case 0x70:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Step out data=0x%x\n",data);
#endif
      FDC.LastCommand=data;
      /* !!!! NOTE StepOut means move towards the centre of the disc - i.e.*/
      /*           decrease track!!!                                       */
      FDC_StepDirCommand(state,-1);
      break;

    case 0x80:
    case 0x90:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Read sector data=0x%x\n",data);
#endif
     /* {
        static int count=4;
        count--;
        if (!count) {
          EnableTrace();
        };
      }; */
      FDC.LastCommand=data;
      FDC_ReadCommand(state);
      break;

    case 0xa0:
    case 0xb0:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Write sector data=0x%x\n",data);
#endif
      FDC.LastCommand=data;
      FDC_WriteCommand(state);
      break;

    case 0xc0:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Read address data=0x%x (PC=0x%x)\n",data,ARMul_GetPC(state));
#endif
      FDC.LastCommand=data;
      FDC_ReadAddressCommand(state);
      break;

    case 0xd0:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Force interrupt data=0x%x\n",data);
#endif
      FDC.LastCommand=data;
      break;

    case 0xe0:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Read track data=0x%x\n",data);
#endif
      FDC.LastCommand=data;
      break;

    case 0xf0:
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_NewCommand: Write track data=0x%x\n",data);
#endif
      FDC.LastCommand=data;
      break;

  }; /* Command type switch */
}; /* FDC_NewCommand */

/*--------------------------------------------------------------------------*/
ARMword FDC_Write(ARMul_State *state, ARMword offset, ARMword data, int bNw) {
  int reg=(offset>>2) &3;

  switch (reg) {
    case 0: /* Command */
      FDC_NewCommand(state,data);
      break;

    case 1: /* Track */
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_Write: Track reg=0x%x\n",data);
#endif
      FDC.Track=data;
      break;

    case 2: /* Sector */
#ifdef DEBUG_FDC
      fprintf(stderr,"FDC_Write: Sector reg=0x%x\n",data);
#endif
      FDC.Sector=data;
      break;

    case 3: /* Data */
      /* fprintf(stderr,"FDC_Write: Data reg=0x%x\n",data); */
      FDC.Data=data;
      switch (FDC.LastCommand & 0xf0) {
        case 0xa0: /* Write sector */
        case 0xb0:
          if (FDC.BytesToGo) {
            int err;
            err=fputc(FDC.Data,FDC.FloppyFile[FDC.CurrentDisc]);
            if (err!=FDC.Data) {
              perror(NULL);
              fprintf(stderr,"FDC_Write: fputc failed!! Data=%d err=%d errno=%d ferror=%d\n",FDC.Data,err,errno,ferror(FDC.FloppyFile[FDC.CurrentDisc]));
              abort();
            };

            if (fflush(FDC.FloppyFile[FDC.CurrentDisc])) fprintf(stderr,"FDC_Write: fflush failed!!\n");
            FDC.BytesToGo--;
          } else {
            fprintf(stderr,"FDC_Write: Data register written for write sector when the whole sector has been received!\n");
          }; /* Already full ? */
          ClearDRQ(state);
          break;
      }; /* Command type switch */
      break;
  };
  return 0;
}; /* FDC_Write */

/*--------------------------------------------------------------------------*/
/* Reopen a floppy drive's image file - mainly so you can flip discs         */
void FDC_ReOpen(ARMul_State *state, int drive) {
#ifdef MACOSX
    char* tmp;
#else
  char tmp[256];
#endif
  
  if (drive>3) return;

  if (FDC.LastCommand!=0xd0) {
    fprintf(stderr,"FDC not idle - can't change floppy\n");
    return;
  };

  /* Close the file if it's open */
  if (FDC.FloppyFile[drive]!=NULL) fclose(FDC.FloppyFile[drive]);

#ifndef MACOSX
#ifdef __riscos__
  sprintf(tmp, "<ArcEm$Dir>.^.FloppyImage%d", drive);
#else
  sprintf(tmp, "FloppyImage%d", drive);
#endif
#else
  tmp = FDC.driveFiles[drive]; 
#endif

  {
    FILE *isThere = fopen(tmp, "r");

    if (isThere) {
      fclose(isThere);
      FDC.FloppyFile[drive] = fopen(tmp,"r+");
    } else {
      FDC.FloppyFile[drive] = NULL;
    }
  }

  if (FDC.FloppyFile[drive]==NULL) {
    /* If it failed for r/w try read only */
    FDC.FloppyFile[drive]=fopen(tmp,"r");
  };

#ifdef DEBUG_FDC
  fprintf(stderr,"FDC_ReOpen: Drive %d %s\n",drive,(FDC.FloppyFile[drive]==NULL)?"unable to reopen":"reopened");
#endif
}; /* FDC_ReOpen */

/*--------------------------------------------------------------------------*/
void FDC_Init(ARMul_State *state) {
    char *env;
    int disc;

    if ((env = getenv("ARCEMFLOPPYFORM")) == NULL) {
        env = "0";
    }
    format = avail_format + atoi(env);
    fprintf(stderr, "floppy format: %s\n", format->name);

  FDC.StatusReg=0;
  FDC.Track=0;
  FDC.Sector = format->sector_base;
  FDC.Data=0;
  FDC.LastCommand=0xd0; /* force interrupt - but actuall not doing */
  FDC.Direction=1;
  FDC.CurrentDisc=0;
  FDC.Sector_ReadAddr = format->sector_base;

#ifndef MACOSX
  /* Read only at the moment */
  for (disc=0;disc<4;disc++) {
    char tmp[256];
#ifdef __riscos__
    sprintf(tmp, "<ArcEm$Dir>.^.FloppyImage%d", disc);

    {
      FILE *isThere = fopen(tmp, "r");
  
      if (isThere) {
        fclose(isThere);
        FDC.FloppyFile[disc] = fopen(tmp,"r+");
      } else {
        FDC.FloppyFile[disc] = NULL;
      }
    }
#else
    sprintf(tmp,"FloppyImage%d",disc);
    FDC.FloppyFile[disc] = fopen(tmp,"r+");
#endif

    if (FDC.FloppyFile[disc] == NULL) {
      /* If it failed for r/w try read only */
      FDC.FloppyFile[disc] = fopen(tmp,"r");
    };
  };
#else
  // Don't load any disc images initially
  for (disc = 0; disc < 4; disc++)
      FDC.driveFiles[disc] = NULL;
#endif
  

  FDC.DelayCount=10000;
  FDC.DelayLatch=10000;
#ifdef DEBUG_FDC
    fprintf(stderr, "FDC_Init: SectorSize=%d Sectors/Track=%d Tracks/Disc=%d Sector offset=%d\n",
        format->bytes_per_sector, format->sectors_per_track,
        format->num_tracks, format->sector_base);
#endif
}; /* FDC_Init */
/*--------------------------------------------------------------------------*/
