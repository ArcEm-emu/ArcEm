/* Emulation of 1772 fdc */
/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */

#define DEBUG_FDC1772 0
#define DBG(a) if (DEBUG_FDC1772) fprintf a

#include <stdlib.h>
#include <string.h>

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
#define READADDRSTART 50
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

static floppy_format avail_format[] = {
    { "ADFS 800KB", 1024, 3, 5, 0, 80 },
    { "DOS 720KB", 512, 2, 9, 1, 80 },
};

/* A temporary method of getting the current drive's format. */
#define CURRENT_FORMAT (FDC.drive[FDC.CurrentDisc].form)

/* give the byte offset of a given sector. */
#define SECTOR_LOC_TO_BYTE_OFF(track, side, sector) \
    (((track * 2 + side) * CURRENT_FORMAT->sectors_per_track + \
        (sector - CURRENT_FORMAT->sector_base)) * \
        CURRENT_FORMAT->bytes_per_sector)

static void FDC_DoWriteChar(ARMul_State *state);
static void FDC_DoReadChar(ARMul_State *state);
static void FDC_DoReadAddressChar(ARMul_State *state);

static void efseek(FILE *fp, long offset, int whence);

/*--------------------------------------------------------------------------*/
static void GenInterrupt(ARMul_State *state, const char *reason) {
  DBG((stderr,"FDC:GenInterrupt: %s\n",reason));
  ioc.FIRQStatus|=2; /* FH1 line on IOC */
  DBG((stderr,"FDC:GenInterrupt FIRQStatus=0x%x Mask=0x%x\n",
    ioc.FIRQStatus,ioc.FIRQMask));
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
  DBG((stderr,"FDC_GenDRQ (data=0x%x)\n",FDC.Data));
  ioc.FIRQStatus|=1; /* FH0 line on IOC */
  IO_UpdateNfiq();
}; /* GenDRQ */

/*--------------------------------------------------------------------------*/
static void ClearDRQ(ARMul_State *state) {
  DBG((stderr,"FDC_ClearDRQ\n"));
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

  DBG((stderr,"LatchA: 0x%x\n",ioc.LatchA));

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
          DBG((stderr,"Floppy drive select %d gone %s\n",bit,val?"High":"Low"));
                if (!val) {
                    FDC.CurrentDisc = bit;
                    DBG((stderr, "setting FDC.Sector = "
                        "FDC.Sector_ReadAddr = %d\n",
                        FDC.drive[FDC.CurrentDisc].form->sector_base));
                    FDC.Sector = FDC.Sector_ReadAddr = 
                        FDC.drive[FDC.CurrentDisc].form->sector_base;
                }
          break;

        case 4:
          DBG((stderr,"Floppy drive side select now %d\n",val?1:0));
          break;

        case 5:
          DBG((stderr,"Floppy drive Motor now %s\n",val?"On":"Off"));
          break;

        case 6:
          now=ARMul_Time;
          diff=now-TimeWhenInUseChanged;
          DBG((stderr,"Floppy In use line now %d (was %s for %lu ticks)\n",
                  val?1:0,val?"low":"high",diff));
          TimeWhenInUseChanged=now;
          break;

        case 7:
          DBG((stderr,"Floppy eject/disc change reset now %d\n",val?1:0));
          break;
      }; /* Bit changed switch */
    }; /* Difference if */
  }; /* bit loop */

    if (diffmask & 0xf && FDC.leds_changed) {
        (*FDC.leds_changed)(~ioc.LatchA & 0xf);
    }

    return;
}; /* FDC_LatchAChange */

/*--------------------------------------------------------------------------*/
void FDC_LatchBChange(ARMul_State *state) {
  int bit;
  int val;
  int diffmask=ioc.LatchB ^ ioc.LatchBold;

  DBG((stderr,"LatchB: 0x%x\n",ioc.LatchB));
  /* Start up test */
  if (ioc.LatchBold==-1) diffmask=0xff;

  for(bit=7;bit>=0;bit--) {
    if (diffmask & (1<<bit)) {
      /* Bit changed! */
      val=ioc.LatchB & (1<<bit);

      switch (bit) {
        case 0:
        case 2:
          DBG((stderr,"Latch B bit %d now %d\n",bit,val?1:0));
          break;

        case 1:
          DBG((stderr,"FDC format now %s Density\n",val?"Single":"Double"));
          break;

        case 3:
          DBG((stderr,"Floppy drive reset now %s\n",val?"High":"Low"));
          break;

        case 4:
          DBG((stderr,"Printer strobe now %d\n",val?1:0));
          break;

        case 5:
          DBG((stderr,"Aux 1 now %d\n",val?1:0));
          break;

        case 6:
          DBG((stderr,"Aux 2 now %d\n",val?1:0));
          break;

        case 7:
          DBG((stderr,"HDC HS3 line now %d\n",val?1:0));
          break;
      }; /* Bit changed switch */
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
      DBG((stderr,"FDC_Read: Status reg=0x%x pc=%08x r[15]=%08x\n",FDC.StatusReg,state->pc,state->Reg[15]));
      ClearInterrupt(state);
      return(FDC.StatusReg);
      break;

    case 1: /* Track */
      DBG((stderr,"FDC_Read: Track reg\n"));
      return(FDC.Track);
      break;

    case 2: /* Sector */
      DBG((stderr,"FDC_Read: Sector reg\n"));
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
    DBG((stderr,"FDC: LostData\n"));
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
    if (FDC.Sector == CURRENT_FORMAT->sectors_per_track +
        CURRENT_FORMAT->sector_base) {
        FDC.Sector = CURRENT_FORMAT->sector_base;
        FDC_NextTrack(state);
    }
}; /* FDC_NextSector */
/*--------------------------------------------------------------------------*/
static void FDC_DoReadAddressChar(ARMul_State *state) {
  DBG((stderr,"FDC_DoReadAddressChar: BytesToGo=%x\n",FDC.BytesToGo));

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
        if (FDC.Sector_ReadAddr >= CURRENT_FORMAT->sectors_per_track +
            CURRENT_FORMAT->sector_base) {
            FDC.Sector_ReadAddr = CURRENT_FORMAT->sector_base;
        }
      break;

    case 3: /* sector length */
        /* 1K per sector (2=512, 1=256, 0=128 */
        FDC.Data = CURRENT_FORMAT->sector_size_code;
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
        DBG((stderr,"FDC: LostData (Read address end)\n"));
      };
      DBG((stderr,"FDC: ReadAddressChar: Terminating command at end\n"));
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
    if (FDC.drive[FDC.CurrentDisc].fp == NULL) {
    data=42;
  } else {
    data = fgetc(FDC.drive[FDC.CurrentDisc].fp);
    if (data==EOF) {
      DBG((stderr,"FDC_DoReadChar: got EOF\n"));
    };
  };
  FDC.Data=data;
  FDC_DoDRQ(state);
  FDC.BytesToGo--;
  if (!FDC.BytesToGo) {
    if (FDC.LastCommand & TYPE2_BIT_MULTISECTOR) {
      FDC_NextSector(state);
      FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector;
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

  if (FDC.Data >= CURRENT_FORMAT->num_tracks) {
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

  if (DesiredTrack >= CURRENT_FORMAT->num_tracks || DesiredTrack < 0) {
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
    long offset;
  int Side=(ioc.LatchA & (1<<4))?0:1; /* Note: Inverted??? Yep!!! */
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_DRQ | BIT_LOSTDATA | (1<<5) | (1<<6) | BIT_RECNOTFOUND);

    offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);
    if (offset < 0) {
        offset = 0;
    }

    if (FDC.drive[FDC.CurrentDisc].fp) {
        efseek(FDC.drive[FDC.CurrentDisc].fp, offset, SEEK_SET);

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

  DBG((stderr,"FDC_ReadCommand: Starting with Side=%d Track=%d Sector=%d (CurrentDisc=%d)\n",
                 Side,FDC.Track,FDC.Sector,FDC.CurrentDisc));

    offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);

    if (FDC.drive[FDC.CurrentDisc].fp) {
        efseek(FDC.drive[FDC.CurrentDisc].fp, offset, SEEK_SET);
    }

    FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector;
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
  if (FDC.BytesToGo > CURRENT_FORMAT->bytes_per_sector) {
    /* Initial DRQ */
    FDC_DoDRQ(state);
    FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector;
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
     FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector;
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

  DBG((stderr,"FDC_WriteCommand: Starting with Side=%d Track=%d Sector=%d\n",
                 Side,FDC.Track,FDC.Sector));

    offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);
    efseek(FDC.drive[FDC.CurrentDisc].fp, offset, SEEK_SET);

  FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector + 1;
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
    case 0x00: /* Restore (Type I command) */
      DBG((stderr,"FDC_NewCommand: Restore data=0x%x pc=%08x r[15]=%08x\n",
              data,state->pc,state->Reg[15]));
      FDC.LastCommand=data;
      FDC_RestoreCommand(state);
      break;

    case 0x10: /* Seek (Type I command) */
      DBG((stderr,"FDC_NewCommand: Seek data=0x%x\n",data));
      FDC.LastCommand=data;
      FDC_SeekCommand(state);
      break;

    case 0x20: /* Step (Type I command) */
    case 0x30:
      DBG((stderr,"FDC_NewCommand: Step data=0x%x\n",data));
      FDC.LastCommand=data;
      FDC_StepDirCommand(state,FDC.Direction);
      break;

    case 0x40: /* Step In (Type I command) */
    case 0x50:
    DBG((stderr,"FDC_NewCommand: Step in data=0x%x\n",data));
    FDC.LastCommand=data;
    /* !!!! NOTE StepIn means move towards the centre of the disc - i.e.*/
    /*           increase track!!!                                      */
      FDC_StepDirCommand(state,+1);
      break;

    case 0x60: /* Step Out (Type I command) */
    case 0x70:
      DBG((stderr,"FDC_NewCommand: Step out data=0x%x\n",data));
      FDC.LastCommand=data;
      /* !!!! NOTE StepOut means move towards the centre of the disc - i.e.*/
      /*           decrease track!!!                                       */
      FDC_StepDirCommand(state,-1);
      break;

    case 0x80: /* Read Sector (Type II command) */
    case 0x90:
      DBG((stderr,"FDC_NewCommand: Read sector data=0x%x\n",data));
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

    case 0xa0: /* Write Sector (Type II command) */
    case 0xb0:
      DBG((stderr,"FDC_NewCommand: Write sector data=0x%x\n",data));
      FDC.LastCommand=data;
      FDC_WriteCommand(state);
      break;

    case 0xc0: /* Read Address (Type III command) */
      DBG((stderr,"FDC_NewCommand: Read address data=0x%x (PC=0x%x)\n",data,ARMul_GetPC(state)));
      FDC.LastCommand=data;
      FDC_ReadAddressCommand(state);
      break;

    case 0xd0: /* Force Interrupt (Type IV command) */
      DBG((stderr,"FDC_NewCommand: Force interrupt data=0x%x\n",data));
      FDC.LastCommand=data;
      break;

    case 0xe0: /* Read Track (Type III command) */
      DBG((stderr,"FDC_NewCommand: Read track data=0x%x\n",data));
      FDC.LastCommand=data;
      break;

    case 0xf0: /* Write Track (Type III command) */
      DBG((stderr,"FDC_NewCommand: Write track data=0x%x\n",data));
      FDC.LastCommand=data;
      break;

  }; /* Command type switch */
}; /* FDC_NewCommand */

/*--------------------------------------------------------------------------*/
ARMword FDC_Write(ARMul_State *state, ARMword offset, ARMword data, int bNw) {
  int reg=(offset>>2) &3;

  switch (reg) {
    case 0: /* Command/Status register */
      FDC_NewCommand(state,data);
      break;

    case 1: /* Track register */
      DBG((stderr,"FDC_Write: Track reg=0x%x\n",data));
      FDC.Track=data;
      break;

    case 2: /* Sector register */
      DBG((stderr,"FDC_Write: Sector reg=0x%x\n",data));
      FDC.Sector=data;
      break;

    case 3: /* Data register */
      /* fprintf(stderr,"FDC_Write: Data reg=0x%x\n",data); */
      FDC.Data=data;
      switch (FDC.LastCommand & 0xf0) {
        case 0xa0: /* Write sector */
        case 0xb0:
          if (FDC.BytesToGo) {
            int err;
                err = fputc(FDC.Data, FDC.drive[FDC.CurrentDisc].fp);
            if (err!=FDC.Data) {
              perror(NULL);
              fprintf(stderr,"FDC_Write: fputc failed!! Data=%d err=%d errno=%d ferror=%d\n",FDC.Data,err,errno,ferror(FDC.drive[FDC.CurrentDisc].fp));
              abort();
            };

                if (fflush(FDC.drive[FDC.CurrentDisc].fp)) {
                    fprintf(stderr, "FDC_Write: fflush failed!!\n");
                }
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

    fdc_eject_floppy(drive);

#if defined(__riscos__)
    sprintf(tmp, "<ArcEm$Dir>.^.FloppyImage%d", drive);
#elif defined(MACOSX)
    tmp = FDC.driveFiles[drive]; 
#else
    sprintf(tmp, "FloppyImage%d", drive);
#endif

    fdc_insert_floppy(drive, tmp);

    DBG((stderr, "FDC_ReOpen: Drive %d %s\n", drive,
        (FDC.drive[drive].fp == NULL) ? "unable to reopen" :
        "reopened"));
}; /* FDC_ReOpen */

/*--------------------------------------------------------------------------*/
void FDC_Init(ARMul_State *state) {
    int drive;

  FDC.StatusReg=0;
  FDC.Track=0;
  FDC.Sector = 0;
  FDC.Data=0;
  FDC.LastCommand=0xd0; /* force interrupt - but actuall not doing */
  FDC.Direction=1;
  FDC.CurrentDisc=0;
    FDC.leds_changed = NULL;
  FDC.Sector_ReadAddr = 0;

    for (drive = 0; drive < 4; drive++) {
#if defined(MACOSX)
        FDC.driveFiles[drive] = NULL;
#endif
        FDC.drive[drive].fp = NULL;
        FDC.drive[drive].form = avail_format;
    }

#if !defined(MACOSX) && !defined(SYSTEM_X)
    for (drive = 0; drive < 4; drive++) {
        char tmp[256];

#if defined(__riscos__)
        sprintf(tmp, "<ArcEm$Dir>.^.FloppyImage%d", drive);
#else
        sprintf(tmp, "FloppyImage%d", drive);
#endif
        fdc_insert_floppy(drive, tmp);
    }
#endif

  FDC.DelayCount=10000;
  FDC.DelayLatch=10000;
}; /* FDC_Init */

/* ------------------------------------------------------------------ */

char *fdc_insert_floppy(int drive, char *image)
{
    floppy_drive *dr;
    FILE *fp;
    int len;
    floppy_format *ff;

    dr = FDC.drive + drive;

    /* FIXME:  shouldn't the read-only status of the file be reflected
     * in the `write protect' state of the floppy? */
    if ((fp = fopen(image, "rb+")) == NULL &&
        (fp = fopen(image, "rb")) == NULL) {
        fprintf(stderr, "couldn't open disc image %s on drive %d\n",
            image, drive);
        return "couldn't open disc image";
    }

    if (fseek(fp, 0, SEEK_END) == -1 || (len = ftell(fp)) == -1 ||
        fseek(fp, 0, SEEK_SET) == -1) {
        fprintf(stderr, "couldn't get length of disc image %s on drive "
            "%d\n", image, drive);
        return "couldn't get length of disc image";
    }

    dr->fp = fp;
    dr->form = avail_format;
    for (ff = avail_format; ff < avail_format +
        (sizeof avail_format / sizeof(avail_format[0])); ff++) {
        if (len == 2 * ff->num_tracks * ff->sectors_per_track *
            ff->bytes_per_sector) {
            dr->form = ff;
            break;
        }
    }
    fprintf(stderr, "floppy format %s used for drive %d's %d length "
        "image.\n", dr->form->name, drive, len);

    return NULL;
}

/* ------------------------------------------------------------------ */

char *fdc_eject_floppy(int drive)
{
    floppy_drive *dr;

    dr = FDC.drive + drive;

    if (fclose(dr->fp)) {
        fprintf(stderr, "error closing floppy drive %d: %s\n",
            drive, strerror(errno));
    }

    dr->fp = NULL;
    /* The code assumes that the format of an, even empty, drive is
     * always known.  Rather than fix all that code, just pretend an
     * empty drive has a known format for the moment. */
    dr->form = avail_format;

    return NULL;
}

/* ------------------------------------------------------------------ */

static void efseek(FILE *fp, long offset, int whence)
{
    if (fseek(fp, offset, whence)) {
        fprintf(stderr, "efseek(%p, %ld, %d) failed.\n", fp, offset,
            whence);
        exit(1);
    }

    return;
}
