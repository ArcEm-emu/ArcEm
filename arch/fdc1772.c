/* Emulation of 1772 fdc */
/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */

/* Some helpful Western Digital FDC 1772 documentation, from a gentle
 * introduction to transcriptions of the data sheet.
 * http://www.atarimagazines.com/startv1n2/ProbingTheFDC.html
 * http://www.cloud9.co.uk/james/BBCMicro/Documentation/wd1770.html
 * http://saint.atari.org/html/doc/wd1772.html
 * http://saint.atari.org/html/doc/wd17722.html
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define __USE_FIXED_PROTOTYPES__
#include <errno.h>
#include <stdio.h>

#include "../armdefs.h"

#include "ArcemConfig.h"
#include "armarc.h"
#include "ControlPane.h"
#include "dbugsys.h"
#include "fdc1772.h"

#define DBG(a) dbug_fdc a


typedef struct {
    /* User-visible name. */
    const char *name;
    uint_least16_t bytes_per_sector;
    uint_least8_t sectors_per_track;
    uint_least8_t sector_base;
    uint_least8_t num_cyl;
    uint_least8_t num_sides;
} floppy_format;

typedef struct {
    /* To access the disc image.  NULL if disc ejected. */
    FILE *fp;
    /* Based on whether read/write access to the disc image was
     * obtained. */
    bool write_protected;
    /* Points to an element of avail_format. */
    const floppy_format *form;
} floppy_drive;

struct FDCStruct{
  uint_least8_t LastCommand;
  int_least8_t Direction; /* -1 or 1 */
  uint_least8_t StatusReg;
  uint_least8_t Track;
  uint_least8_t Sector;
  uint_least8_t Sector_ReadAddr; /* Used to hold the next sector to be found! */
  uint_least8_t Data;
  uint_least8_t CurrentDisc;
  int_least16_t BytesToGo;
  int32_t DelayCount;
  int32_t DelayLatch;
    floppy_drive drive[4];
    /* The bottom four bits of leds holds their current state.  If the
     * bit is set the LED should be emitting. */
    void (*leds_changed)(uint_fast8_t leds);
};


/* Type I commands. */
#define CMD_RESTORE 0x00
#define CMD_RESTORE_MASK 0xf0
#define CMD_SEEK 0x10
#define CMD_SEEK_MASK 0xf0
#define CMD_STEP 0x20
#define CMD_STEP_MASK 0xe0
#define CMD_STEP_IN 0x40
#define CMD_STEP_IN_MASK 0xe0
#define CMD_STEP_OUT 0x60
#define CMD_STEP_OUT_MASK 0xe0
/* Type II commands. */
#define CMD_READ_SECTOR 0x80
#define CMD_READ_SECTOR_MASK 0xe0
#define CMD_WRITE_SECTOR 0xa0
#define CMD_WRITE_SECTOR_MASK 0xe0
/* Type III commands. */
#define CMD_READ_ADDR 0xc0
#define CMD_READ_ADDR_MASK 0xf0
#define CMD_READ_TRACK 0xe0
#define CMD_READ_TRACK_MASK 0xf0
#define CMD_WRITE_TRACK 0xf0
#define CMD_WRITE_TRACK_MASK 0xf0
/* Type IV commands. */
#define CMD_FORCE_INTR 0xd0
#define CMD_FORCE_INTR_MASK 0xf0

#define IS_CMD(data, cmd) \
    (((data) & CMD_ ## cmd ## _MASK) == CMD_ ## cmd)

/* Assuming we get called every 250 cycles by FDCHDC_Poll, these are sensible delay counters for us to use: */
#define READSPACING MAX(1,(ARMul_EmuRate/(250*31250))) /* 250kbps data rate */
#define WRITESPACING MAX(1,(ARMul_EmuRate/(250*31250)))
#define READADDRSTART MAX(50,(ARMul_EmuRate/(250*50))) /* At 300RPM, and 5 sectors per track, that's 1/25th of a second between each sector. But use a delay 1/50th since we'll usually be in the area between two sectors */
#define SEEKDELAY MAX(1,(ARMul_EmuRate/(250*31250)))

#define BIT_BUSY 1
#define BIT_DRQ (1<<1)
#define BIT_TR00 (1<<2)
#define BIT_LOSTDATA (1<<2)
#define BIT_CRC (1<<3)
#define BIT_RECNOTFOUND (1<<4)
#define BIT_MOTORSPINUP (1<<5)
#define BIT_WRITEPROT (1 << 6)
#define BIT_MOTORON (1<<7)
#define TYPE1_UPDATETRACK (1<<4)
#define TYPE2_BIT_MOTORON (1<<2)
#define TYPE2_BIT_MULTISECTOR (1<<4)

/* Structure containing the state of the floppy drive controller */
struct FDCStruct FDC;


static const floppy_format avail_format[] = {
    /* First is default for `ejected' discs. */
    { "ADFS 800KB", 1024, 5, 0, 80, 2 },
    { "ADFS 160KB", 256, 16, 0, 40, 1 },
    { "ADFS 320KB", 256, 16, 0, 80, 1 },
    { "ADFS 640KB", 256, 16, 0, 80, 2 },
    { "DOS 360KB", 512, 9, 1, 40, 2 },
    { "DOS 720KB", 512, 9, 1, 80, 2 },
};

/* A temporary method of getting the current drive's format. */
#define CURRENT_FORMAT (FDC.drive[FDC.CurrentDisc].form)

/* give the byte offset of a given sector. */
#define SECTOR_LOC_TO_BYTE_OFF(cyl, side, sector) \
    (((cyl * CURRENT_FORMAT->num_sides + side) * \
        CURRENT_FORMAT->sectors_per_track + \
        (sector - CURRENT_FORMAT->sector_base)) * \
        CURRENT_FORMAT->bytes_per_sector)

static void FDC_DoWriteChar(ARMul_State *state);
static void FDC_DoReadChar(ARMul_State *state);
static void FDC_DoReadAddressChar(ARMul_State *state);

static void efseek(FILE *fp, long offset, int whence);

/*--------------------------------------------------------------------------*/
static void GenInterrupt(ARMul_State *state, const char *reason) {
  DBG(("FDC:GenInterrupt: %s\n",reason));
  ioc.FIRQStatus |= FIQ_FDIRQ; /* FH1 line on IOC */
  DBG(("FDC:GenInterrupt FIRQStatus=0x%x Mask=0x%x\n",
    ioc.FIRQStatus,ioc.FIRQMask));
  IO_UpdateNfiq(state);
} /* GenInterrupt */


/**
 * FDC_Regular
 *
 * Called regulaly from the DispKbd poll loop.
 *
 * @param state State of the emulator
 */
void FDC_Regular(ARMul_State *state)
{
  int_fast16_t ActualTrack;

  if (--FDC.DelayCount) return;

    if (IS_CMD(FDC.LastCommand, RESTORE)) {
      FDC.StatusReg|=BIT_MOTORON | BIT_MOTORSPINUP | BIT_TR00;
      FDC.Track=0;
      GenInterrupt(state,"Restore complete");
        FDC.LastCommand = CMD_FORCE_INTR;
      FDC.StatusReg&=~BIT_BUSY;

    } else if (IS_CMD(FDC.LastCommand, SEEK)) {
      /* It will complete now. */
        FDC.LastCommand = CMD_FORCE_INTR;
      FDC.StatusReg&=~BIT_BUSY;
      FDC.StatusReg|=BIT_MOTORSPINUP|BIT_MOTORON;
      FDC.Track=FDC.Data; /* Got to the desired track */
      if (FDC.Track==0) FDC.StatusReg|=BIT_TR00;
      GenInterrupt(state,"Seek complete");

    } else if (IS_CMD(FDC.LastCommand, STEP) ||
        IS_CMD(FDC.LastCommand, STEP_IN) ||
        IS_CMD(FDC.LastCommand, STEP_OUT)) {
      /* It will complete now. */
      FDC.StatusReg&=~BIT_BUSY;
      FDC.StatusReg|=BIT_MOTORSPINUP|BIT_MOTORON;
      ActualTrack=FDC.Track+FDC.Direction;
      if (FDC.LastCommand & TYPE1_UPDATETRACK)
        FDC.Track=ActualTrack; /* Got to the desired track */
      if (ActualTrack==0) FDC.StatusReg|=BIT_TR00;
        FDC.LastCommand = CMD_FORCE_INTR;
      GenInterrupt(state,"Step complete");

    } else if (IS_CMD(FDC.LastCommand, READ_SECTOR)) {
      /* Next character. */
      if (FDC.BytesToGo) FDC_DoReadChar(state);
      FDC.DelayCount=FDC.DelayLatch;

    } else if (IS_CMD(FDC.LastCommand, WRITE_SECTOR)) {
      /* Next character. */
      FDC_DoWriteChar(state);
      FDC.DelayCount=FDC.DelayLatch;

    } else if (IS_CMD(FDC.LastCommand, READ_ADDR)) {
      FDC_DoReadAddressChar(state);
      FDC.DelayCount=FDC.DelayLatch;
    }

    return;
}

/*--------------------------------------------------------------------------*/
static void ClearInterrupt(ARMul_State *state) {
  ioc.FIRQStatus &= ~FIQ_FDIRQ; /* FH1 line on IOC */
  IO_UpdateNfiq(state);
} /* ClearInterrupt */

/*--------------------------------------------------------------------------*/
static void GenDRQ(ARMul_State *state) {
  DBG(("FDC_GenDRQ (data=0x%x)\n",FDC.Data));
  ioc.FIRQStatus |= FIQ_FDDRQ; /* FH0 line on IOC */
  IO_UpdateNfiq(state);
} /* GenDRQ */

/*--------------------------------------------------------------------------*/
static void ClearDRQ(ARMul_State *state) {
  DBG(("FDC_ClearDRQ\n"));
  ioc.FIRQStatus &= ~FIQ_FDDRQ; /* FH0 line on IOC */
  IO_UpdateNfiq(state);
  FDC.StatusReg&=~BIT_DRQ;
} /* ClearDRQ */

/**
 * FDC_LatchAChange
 *
 * Callback function, whenever LatchA is written too (regardless of
 * whether the value has changed).
 *
 * @param state Emulator state
 */
void FDC_LatchAChange(ARMul_State *state) {
  static CycleCount TimeWhenInUseChanged;
  CycleCount now,diff;
  int_fast8_t bit;
  uint_fast8_t val;
  uint_fast8_t diffmask=ioc.LatchA ^ ioc.LatchAold;

  DBG(("LatchA: 0x%x\n",ioc.LatchA));

  /* Start up test */
  if (ioc.LatchAold>0xff) {
    diffmask=0xff;
  }

  for(bit=7;bit>=0;bit--) {
    if (diffmask & (1<<bit)) {
      /* Bit changed! */
      val = ioc.LatchA & (1 << bit);

      switch (bit) {
        case 0:
        case 1:
        case 2:
        case 3:
          DBG(("Floppy drive select %d gone %s\n",bit,val?"High":"Low"));
                if (!val) {
                    FDC.CurrentDisc = bit;
                    DBG(("setting FDC.Sector = "
                        "FDC.Sector_ReadAddr = %d\n",
                        FDC.drive[FDC.CurrentDisc].form->sector_base));
                    FDC.Sector = FDC.Sector_ReadAddr =
                        FDC.drive[FDC.CurrentDisc].form->sector_base;
                }
          break;

        case 4:
          DBG(("Floppy drive side select now %d\n",val?1:0));
          break;

        case 5:
          DBG(("Floppy drive Motor now %s\n",val?"On":"Off"));
          break;

        case 6:
          now=ARMul_Time;
          diff=now-TimeWhenInUseChanged;
          DBG(("Floppy In use line now %d (was %s for %"PRIu32" ticks)\n",
                  val?1:0,val?"low":"high",diff));
          TimeWhenInUseChanged=now;
          break;

        case 7:
          DBG(("Floppy eject/disc change reset now %d\n",val?1:0));
          break;
      } /* Bit changed switch */
    } /* Difference if */
  } /* bit loop */

    if (diffmask & 0xf && FDC.leds_changed) {
        (*FDC.leds_changed)(~ioc.LatchA & 0xf);
    }

    return;
} /* FDC_LatchAChange */

/**
 * FDC_LatchBChange
 *
 * Callback function, whenever LatchA is written too (regardless of
 * whether the value has changed).
 *
 * @param state Emulator state
 */
void FDC_LatchBChange(ARMul_State *state) {
  int_fast8_t bit;
  uint_fast8_t val;
  uint_fast8_t diffmask=ioc.LatchB ^ ioc.LatchBold;

  UNUSED_VAR(state);

  DBG(("LatchB: 0x%x\n",ioc.LatchB));
  /* Start up test */
  if (ioc.LatchBold>0xff) {
    diffmask=0xff;
  }

  for(bit=7;bit>=0;bit--) {
    if (diffmask & (1<<bit)) {
      /* Bit changed! */
      val=ioc.LatchB & (1<<bit);

      switch (bit) {
        case 0:
        case 2:
          DBG(("Latch B bit %d now %d\n",bit,val?1:0));
          break;

        case 1:
          DBG(("FDC format now %s Density\n",val?"Single":"Double"));
          break;

        case 3:
          DBG(("Floppy drive reset now %s\n",val?"High":"Low"));
          break;

        case 4:
          DBG(("Printer strobe now %d\n",val?1:0));
          break;

        case 5:
          DBG(("Aux 1 now %d\n",val?1:0));
          break;

        case 6:
          DBG(("Aux 2 now %d\n",val?1:0));
          break;

        case 7:
          DBG(("HDC HS3 line now %d\n",val?1:0));
          break;
      } /* Bit changed switch */
    } /* Difference if */
  } /* bit loop */
} /* FDC_LatchBChange */
/*--------------------------------------------------------------------------*/

static void ReadDataRegSpecial(ARMul_State *state)
{
  if (IS_CMD(FDC.LastCommand, READ_SECTOR)) {
    /* If we just read the last piece of data we can issue a completed
     * interrupt. */
    if (!FDC.BytesToGo) {
      GenInterrupt(state,"Read end (b)");
      /* Force int with no interrupt. */
      FDC.LastCommand = CMD_FORCE_INTR;
      FDC.StatusReg&=~BIT_BUSY;
    }

  } else if (IS_CMD(FDC.LastCommand, READ_ADDR)) {
    /* If we just read the last piece of data we can issue a completed
     * interrupt. */
    if (!FDC.BytesToGo) {
      GenInterrupt(state,"Read addr end");
      /* Force int with no interrupt. */
      FDC.LastCommand = CMD_FORCE_INTR;
      FDC.StatusReg&=~BIT_BUSY;
      /* Supposed to copy the track into the sector register */
      FDC.Sector=FDC.Track;
    }
  }

  return;
}

/**
 * FDC_Read
 *
 * Read from the 1772 FDC controller's registers
 *
 * @param state  Emulator State
 * @param offset Containing the FDC register
 * @returns      Value of register, or 0 on register not handled
 */
uint_fast8_t FDC_Read(ARMul_State *state, uint_fast16_t offset) {
  uint_fast8_t reg=(offset>>2) &3;

  switch (reg) {
    case 0: /* Status */
      DBG(("FDC_Read: Status reg=0x%x pc=%08x r[15]=%08x\n",FDC.StatusReg,state->pc,state->Reg[15]));
      ClearInterrupt(state);
      return(FDC.StatusReg);
      break;

    case 1: /* Track */
      DBG(("FDC_Read: Track reg\n"));
      return(FDC.Track);
      break;

    case 2: /* Sector */
      DBG(("FDC_Read: Sector reg\n"));
      return(FDC.Sector);
      break;

    case 3: /* Data */
      /*DBG(("FDC_Read: Data reg=0x%x (BytesToGo=%d)\n",FDC.Data,FDC.BytesToGo)); */
      ClearDRQ(state);
      ReadDataRegSpecial(state);
      return(FDC.Data);
      break;
  }

  return(0);
} /* FDC_Read */

/*--------------------------------------------------------------------------*/
static void FDC_DoDRQ(ARMul_State *state) {
  /* If they haven't read the previous data then flag a problem */
  if (FDC.StatusReg & BIT_DRQ) {
    FDC.StatusReg |=BIT_LOSTDATA;
    DBG(("FDC: LostData\n"));
  }

  FDC.StatusReg|=BIT_DRQ;
  GenDRQ(state);
} /* FDC_DoDRQ */

/*--------------------------------------------------------------------------*/

static void FDC_NextTrack(ARMul_State *state)
{
  UNUSED_VAR(state);

  if (FDC.Track < CURRENT_FORMAT->num_cyl) {
      FDC.Track++;
  }

  return;
}

/*--------------------------------------------------------------------------*/
static void FDC_NextSector(ARMul_State *state) {
  FDC.Sector++;
  if (FDC.Sector == CURRENT_FORMAT->sectors_per_track +
      CURRENT_FORMAT->sector_base) {
      FDC.Sector = CURRENT_FORMAT->sector_base;
      FDC_NextTrack(state);
  }
} /* FDC_NextSector */
/*--------------------------------------------------------------------------*/
static void FDC_DoReadAddressChar(ARMul_State *state) {
  uint_fast8_t bps;

  DBG(("FDC_DoReadAddressChar: BytesToGo=%x\n",FDC.BytesToGo));

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
      FDC.Data = 0;
      for (bps = CURRENT_FORMAT->bytes_per_sector >> 8; bps;
          bps >>= 1) {
          FDC.Data++;
      }
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
        DBG(("FDC: LostData (Read address end)\n"));
      }
      DBG(("FDC: ReadAddressChar: Terminating command at end\n"));
      GenInterrupt(state,"Read end");
        /* Force int with no interrupt. */
        FDC.LastCommand = CMD_FORCE_INTR;
      FDC.StatusReg&=~BIT_BUSY;
      /* Supposed to copy the track into the sector register */
      FDC.Sector=FDC.Track;
      break;
  } /* Bytes left */

  FDC_DoDRQ(state);
  FDC.BytesToGo--;
} /* FDC_DoReadAddressChar */

/*--------------------------------------------------------------------------*/
static void FDC_DoReadChar(ARMul_State *state) {
  int data;
 
  if (FDC.drive[FDC.CurrentDisc].fp == NULL) {
    data=42;
  } else {
    data = fgetc(FDC.drive[FDC.CurrentDisc].fp);
    if (data==EOF) {
      DBG(("FDC_DoReadChar: got EOF\n"));
    }
  }
  
  FDC.Data=data & 0xff;
  FDC_DoDRQ(state);
  FDC.BytesToGo--;
  if (!FDC.BytesToGo) {
    if (FDC.LastCommand & TYPE2_BIT_MULTISECTOR) {
      FDC_NextSector(state);
      FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector;
    } else {
      /* We'll actually terminate on next data read */
    } /* Not multisector */
  }
} /* FDC_DoReadChar */

/*--------------------------------------------------------------------------*/
/* Type 1 command, h/V/r1,r0 - desired track in data reg                    */
static void FDC_SeekCommand(ARMul_State *state) {
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_CRC|BIT_RECNOTFOUND|BIT_TR00);

  ClearInterrupt(state);
  ClearDRQ(state);

  if (FDC.Data >= CURRENT_FORMAT->num_cyl) {
    /* Fail!! */
    FDC.StatusReg|=BIT_RECNOTFOUND;
    GenInterrupt(state,"Seek fail");
        FDC.LastCommand = CMD_FORCE_INTR;
    FDC.StatusReg&=~BIT_BUSY;
  }
  FDC.DelayCount=FDC.DelayLatch=SEEKDELAY;
} /* FDC_SeekCommand */

/*--------------------------------------------------------------------------*/
/* Type 1 command, u/h/V/r1,r0 -                                            */
static void FDC_StepDirCommand(ARMul_State *state,int_fast8_t Dir) {
  int_fast8_t DesiredTrack=FDC.Track+Dir;
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_CRC|BIT_RECNOTFOUND|BIT_TR00);

  ClearInterrupt(state);
  ClearDRQ(state);

  FDC.Direction=Dir;

  if (DesiredTrack >= CURRENT_FORMAT->num_cyl || DesiredTrack < 0) {
    /* Fail!! */
    FDC.StatusReg|=BIT_RECNOTFOUND;
    GenInterrupt(state,"Step fail");
        FDC.LastCommand = CMD_FORCE_INTR;
    FDC.StatusReg&=~BIT_BUSY;
  }
  FDC.DelayCount=FDC.DelayLatch=SEEKDELAY;
} /* FDC_StepDirCommand */

/*--------------------------------------------------------------------------*/
static void FDC_ReadAddressCommand(ARMul_State *state) {
  long offset;
  uint_fast8_t Side=(ioc.LatchA & (1<<4))?0:1; /* Note: Inverted??? Yep!!! */

  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg &= ~(BIT_DRQ | BIT_LOSTDATA | (1<<5) | BIT_WRITEPROT | BIT_RECNOTFOUND);

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
    FDC.LastCommand = CMD_FORCE_INTR;
    FDC.StatusReg&=~BIT_BUSY;
  }
} /* ReadAddressCommand */

/*--------------------------------------------------------------------------*/
static void FDC_ReadCommand(ARMul_State *state) {
  long offset;
  uint_fast8_t Side=(ioc.LatchA & (1<<4))?0:1; /* Note: Inverted??? Yep!!! */

  UNUSED_VAR(state);

  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg &= ~(BIT_DRQ | BIT_LOSTDATA | (1<<5) | BIT_WRITEPROT | BIT_RECNOTFOUND);

  DBG(("FDC_ReadCommand: Starting with Side=%u Track=%d Sector=%d (CurrentDisc=%d)\n",
                 Side,FDC.Track,FDC.Sector,FDC.CurrentDisc));

  offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);

  if (FDC.drive[FDC.CurrentDisc].fp) {
    efseek(FDC.drive[FDC.CurrentDisc].fp, offset, SEEK_SET);
  }

  FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector;
  /* FDC_DoReadChar(state); - let the regular code do this */
  FDC.DelayCount=FDC.DelayLatch=READSPACING;
} /* FDC_ReadCommand */

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
  }

  if (FDC.BytesToGo!=0) {
    FDC_DoDRQ(state);
    return;
  }

  /* OK - this is the final case - end of the sector */
  /* but if its a multi sector command then we just have to carry on */
  if (FDC.LastCommand & TYPE2_BIT_MULTISECTOR) {
     FDC_NextSector(state);
     FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector;
     FDC_DoDRQ(state);
  } else {
    /* really the end */
    GenInterrupt(state,"end write");
    /* Force int with no interrupt. */
    FDC.LastCommand = CMD_FORCE_INTR;
    FDC.StatusReg&=~BIT_BUSY;
    ClearDRQ(state);
  }
} /* FDC_DoWriteChar */
/*--------------------------------------------------------------------------*/
static void FDC_WriteCommand(ARMul_State *state) {
  long offset;
  uint_fast8_t Side=(ioc.LatchA & (1<<4))?0:1; /* Note: Inverted??? Yep!!! */
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg &= ~(BIT_DRQ | BIT_LOSTDATA | (1<<5) | BIT_WRITEPROT | BIT_RECNOTFOUND);

  DBG(("FDC_WriteCommand: Starting with Side=%u Track=%d Sector=%d\n",
                 Side,FDC.Track,FDC.Sector));

  if (FDC.drive[FDC.CurrentDisc].write_protected) {
    FDC.StatusReg |= BIT_WRITEPROT;
    FDC.StatusReg &= ~BIT_BUSY;
    FDC.LastCommand = CMD_FORCE_INTR;
    GenInterrupt(state, "disc write protected");
    return;
  }

  offset = SECTOR_LOC_TO_BYTE_OFF(FDC.Track, Side, FDC.Sector);
  efseek(FDC.drive[FDC.CurrentDisc].fp, offset, SEEK_SET);

  FDC.BytesToGo = CURRENT_FORMAT->bytes_per_sector + 1;
  /*GenDRQ(state); */ /* Please mister host - give me some data! - no that should happen on the regular!*/
  FDC.DelayCount=FDC.DelayLatch=WRITESPACING;
} /* FDC_WriteCommand */
/*--------------------------------------------------------------------------*/
static void FDC_RestoreCommand(ARMul_State *state) {
  UNUSED_VAR(state);
  FDC.StatusReg|=BIT_BUSY;
  FDC.StatusReg&=~(BIT_RECNOTFOUND | BIT_CRC | BIT_DRQ);
  FDC.DelayCount=FDC.DelayLatch=READSPACING;
} /* FDC_RestoreCommand */
/*--------------------------------------------------------------------------*/

static void FDC_NewCommand(ARMul_State *state, uint_fast8_t data)
{
  ClearInterrupt(state);

  if (IS_CMD(data, RESTORE)) {
    DBG(("FDC_NewCommand: Restore data=0x%x pc=%08x r[15]=%08x\n",
            data,state->pc,state->Reg[15]));
    FDC.LastCommand=data;
    FDC_RestoreCommand(state);
  } else if (IS_CMD(data, SEEK)) {
    DBG(("FDC_NewCommand: Seek data=0x%x\n",data));
    FDC.LastCommand=data;
    FDC_SeekCommand(state);
  } else if (IS_CMD(data, STEP)) {
    DBG(("FDC_NewCommand: Step data=0x%x\n",data));
    FDC.LastCommand=data;
    FDC_StepDirCommand(state,FDC.Direction);
  } else if (IS_CMD(data, STEP_IN)) {
  DBG(("FDC_NewCommand: Step in data=0x%x\n",data));
  FDC.LastCommand=data;
  /* !!!! NOTE StepIn means move towards the centre of the disc - i.e.*/
  /*           increase track!!!                                      */
    FDC_StepDirCommand(state,+1);
  } else if (IS_CMD(data, STEP_OUT)) {
    DBG(("FDC_NewCommand: Step out data=0x%x\n",data));
    FDC.LastCommand=data;
    /* !!!! NOTE StepOut means move towards the centre of the disc - i.e.*/
    /*           decrease track!!!                                       */
    FDC_StepDirCommand(state,-1);
  } else if (IS_CMD(data, READ_SECTOR)) {
    DBG(("FDC_NewCommand: Read sector data=0x%x\n",data));
   /* {
      static int count=4;
      count--;
      if (!count) {
        EnableTrace();
      };
    }; */
    FDC.LastCommand=data;
    FDC_ReadCommand(state);
  } else if (IS_CMD(data, WRITE_SECTOR)) {
    DBG(("FDC_NewCommand: Write sector data=0x%x\n",data));
    FDC.LastCommand=data;
    FDC_WriteCommand(state);
  } else if (IS_CMD(data, READ_ADDR)) {
    DBG(("FDC_NewCommand: Read address data=0x%x (PC=0x%x)\n",data,ARMul_GetPC(state)));
    FDC.LastCommand=data;
    FDC_ReadAddressCommand(state);
  } else if (IS_CMD(data, READ_TRACK)) {
    DBG(("FDC_NewCommand: Read track data=0x%x\n",data));
    FDC.LastCommand=data;
  } else if (IS_CMD(data, WRITE_TRACK)) {
    DBG(("FDC_NewCommand: Write track data=0x%x\n",data));
    FDC.LastCommand=data;
  } else if (IS_CMD(data, FORCE_INTR)) {
    DBG(("FDC_NewCommand: Force interrupt data=0x%x\n",data));
    FDC.LastCommand=data;
  } else {
    /* warn_fdc("unknown FDC command received: %#x\n", data); */
  }

  return;
}

/**
 * FDC_Write
 *
 * Write to the 1772 FDC controller's registers
 *
 * @param state  Emulator State
 * @param offset Containing the FDC register
 * @param data   Data field to write
 */
void FDC_Write(ARMul_State *state, uint_fast16_t offset, uint_fast8_t data) {
  uint_fast8_t reg=(offset>>2) &3;

  switch (reg) {
    case 0: /* Command/Status register */
      FDC_NewCommand(state,data);
      break;

    case 1: /* Track register */
      DBG(("FDC_Write: Track reg=0x%x\n",data));
      FDC.Track=data;
      break;

    case 2: /* Sector register */
      DBG(("FDC_Write: Sector reg=0x%x\n",data));
      FDC.Sector=data;
      break;

    case 3: /* Data register */
      /* DBG(("FDC_Write: Data reg=0x%x\n",data)); */
      FDC.Data=data;
        if (IS_CMD(FDC.LastCommand, WRITE_SECTOR)) {
          if (FDC.BytesToGo) {
            int err;

            err = fputc(FDC.Data, FDC.drive[FDC.CurrentDisc].fp);
            
            if (err!=FDC.Data) {
              ControlPane_Error(true,"FDC_Write: fputc failed!! Data=%d err=%d ferror=%d: %s",FDC.Data,err,ferror(FDC.drive[FDC.CurrentDisc].fp),strerror(errno));
            }

            if (fflush(FDC.drive[FDC.CurrentDisc].fp)) {
              warn_fdc("FDC_Write: fflush failed!!\n");
            }
            FDC.BytesToGo--;
          } else {
            warn_fdc("FDC_Write: Data register written for write sector when the whole sector has been received!\n");
          } /* Already full ? */
          ClearDRQ(state);
        }
      break;
  }
} /* FDC_Write */

/**
 * FDC_Init
 *
 * Called on program startup, initialise the 1772 disk controller
 *
 * @param state Emulator state IMPROVE unused
 */
void FDC_Init(ARMul_State *state) {
  uint_fast8_t drive;

  FDC.StatusReg=0;
  FDC.Track=0;
  FDC.Sector = 0;
  FDC.Data=0;
  FDC.LastCommand = CMD_FORCE_INTR; /* Force interrupt - but actually
                                     * not doing. */
  FDC.Direction=1;
  FDC.CurrentDisc=0;
  FDC.leds_changed = NULL;
  FDC.Sector_ReadAddr = 0;

  for (drive = 0; drive < 4; drive++) {
    FDC.drive[drive].fp = NULL;
    FDC.drive[drive].form = avail_format;
  }

  for (drive = 0; drive < 4; drive++) {
    char *FileName = CONFIG.aFloppyPaths[drive];
    if (!FileName)
        continue;

    FDC_InsertFloppy(drive, FileName);

  }

  FDC.DelayCount=10000;
  FDC.DelayLatch=10000;
} /* FDC_Init */

/**
 * FDC_InsertFloppy
 *
 * Associate disc image with drive.Drive must be empty
 * on startup or having been previously ejected.
 *
 * @oaram drive Drive number to load image into [0-3]
 * @param image Filename of image to load
 * @returns NULL on success or string of error message
 */
const char *
FDC_InsertFloppy(uint_fast8_t drive, const char *image)
{
  floppy_drive *dr;
  FILE *fp;
  long len;
  const floppy_format *ff;

  assert(drive < sizeof FDC.drive / sizeof FDC.drive[0]);
  assert(image);

  if (FDC.LastCommand != CMD_FORCE_INTR) {
    warn_fdc("fdc busy (%#x), can't insert floppy.\n",
            FDC.LastCommand);
    return "fdc busy, try again later";
  }

  dr = FDC.drive + drive;

  assert(dr->fp == NULL);

  if ((fp = fopen(image, "rb+")) != NULL) {
    dr->write_protected = false;
  } else if ((fp = fopen(image, "rb")) != NULL) {
    dr->write_protected = true;
  } else {
    warn_fdc("couldn't open disc image %s on drive %u\n",
          image, drive);
    return "couldn't open disc image";
  }

  if (fseek(fp, 0, SEEK_END) == -1 || (len = ftell(fp)) == -1 ||
      fseek(fp, 0, SEEK_SET) == -1)
  {
    warn_fdc("couldn't get length of disc image %s on drive "
            "%u\n", image, drive);
    return "couldn't get length of disc image";
  }

  dr->fp = fp;
  dr->form = avail_format;
  for (ff = avail_format; ff < avail_format +
      (sizeof avail_format / sizeof(avail_format[0])); ff++)
  {
    if (len == ff->num_sides * ff->num_cyl * ff->sectors_per_track *
        ff->bytes_per_sector)
    {
      dr->form = ff;
      break;
    }
  }
  warn_fdc("floppy format %s used for drive %u's r/%c, %d "
          "length, image.\n", dr->form->name, drive,
          dr->write_protected ? 'o' : 'w', len);

  return NULL;
}

/**
 * FDC_EjectFloppy
 *
 * Close and forget about the disc image associated with drive.  Disc
 * must be inserted.
 *
 * @param drive Drive number to unload image [0-3]
 * @returns NULL on success or string of error message
 */
const char *
FDC_EjectFloppy(uint_fast8_t drive)
{
  floppy_drive *dr;

  assert(drive < sizeof FDC.drive / sizeof FDC.drive[0]);

  dr = FDC.drive + drive;

/* assert(NULL) crashes ArcEm on the Amiga, this gives us a quick clean exit
   from FDC_EjectFloppy when no disc is present */
  if (!dr->fp) {
    warn_fdc("no disc in floppy drive %u: %s\n",
            drive, strerror(errno));
	return(NULL);
  }

  assert(dr->fp);

  if (fclose(dr->fp)) {
    warn_fdc("error closing floppy drive %u: %s\n",
            drive, strerror(errno));
  }

  dr->fp = NULL;
  /* The code assumes that the format of an, even empty, drive is
   * always known.  Rather than fix all that code, just pretend an
   * empty drive has a known format for the moment. */
  dr->form = avail_format;

  return NULL;
}

/**
 * FDC_IsFloppyInserted
 *
 * Check if there's a floppy disc inserted in the specified drive.
 *
 * @param drive Drive number to check [0-3]
 * @returns true if a disc is inserted, false otherwise
 */
bool
FDC_IsFloppyInserted(uint_fast8_t drive)
{
    floppy_drive* dr;

    assert(drive < sizeof FDC.drive / sizeof FDC.drive[0]);

    dr = FDC.drive + drive;

    return (dr->fp != NULL);
}

/* ------------------------------------------------------------------ */

static void efseek(FILE *fp, long offset, int whence)
{
  if (fseek(fp, offset, whence)) {
    ControlPane_Error(true,"efseek(%p, %ld, %d) failed.", fp, offset,
            whence);
  }

  return;
}

/**
 * FDC_SetLEDsChangeFunc
 *
 * If your platform wants to it can assign a function
 * to be called back each time the Floppy Drive LEDs
 * change.
 * Each time they change the callback function will recieve
 * the state of the LEDs in the 'leds' parameter. See
 * X/ControlPane.c  draw_floppy_leds() for an example of
 * how to process the parameter.
 *
 * @param leds_changed Function to callback on LED changes
 */
void FDC_SetLEDsChangeFunc(void (*leds_changed)(uint_fast8_t))
{
  assert(leds_changed);
  
  FDC.leds_changed = leds_changed;
}
