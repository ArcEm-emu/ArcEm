/* The hard drive controller as used in the A4x0 and on the podule for
   A3x0's.                                                       
    
   (C) David Alan Gilbert 1995-1999
*/

/* A key point of confusion throughout the source and documentation is
 * that hArcemConfig.aST506DiskShapes is an array of four elements so
 * I'd assume that [0] matched onto ADFS::4 and [1] was ADFS::5.  (An
 * ST506 controller only supported two drives.)  So you'd think that the
 * "MFM disc" line in ~/.arcemrc had to specify 0 and 1 as they're used
 * as indexes into aST506DiskShapes[].
 *
 * But it seems that the commands RISC OS sends to the ST506 controller
 * give a two-bit drive number that's either 1 or 2.  Therefore
 * aST506DiskShapes[0] is never accessed and the "MFM disc" line should
 * specify the geometry of drive 1 for ADFS::4 and that image file
 * should be called "HardImage1". */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../armdefs.h"

#include "dbugsys.h"
#include "hdc63463.h"
#include "ArcemConfig.h"
#include "ControlPane.h"

struct HDCReadDataStr {
  uint8_t US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL;
  uint32_t BuffersLeft;
  uint32_t NextDestBuffer;
};

struct HDCWriteDataStr {
  uint8_t US,PHA,SCNTH,SCNTL,LSA,LHA,LCAL,LCAH;
  uint32_t BuffersLeft;
  uint32_t CurrentSourceBuffer;
};

struct HDCWriteFormatStr {
  uint8_t US,PHA,SCNTH,SCNTL;
  uint32_t SectorsLeft;
  uint32_t CurrentSourceBuffer;
};

union HDCCommandDataStr {
  struct HDCReadDataStr ReadData;
  struct HDCWriteFormatStr WriteFormat;
  struct HDCWriteDataStr WriteData;
};

struct HDCStruct {
  FILE *HardFile[4];
  int_fast16_t LastCommand; /* -1=idle, 0xfff=command execution complete, other=command busy */
  uint8_t StatusReg;
  uint32_t Track[4];
  uint32_t Sector;
  bool DREQ;
  int8_t CurrentlyOpenDataBuffer;
  uint_fast16_t DBufPtrs[2];
  uint8_t DBufs[2][256];
  uint32_t PBPtr;
  uint8_t ParamBlock[16];
  bool HaveGotSpecify;    /* True once specify received */
  uint8_t SSB; /* Seems to hold error reports */

  int DelayCount;
  int DelayLatch;

  union HDCCommandDataStr CommandData;

  /* Stuff set in specify command */
  uint8_t dmaNpio;
  uint8_t CEDint; /* interrupt masks - >> 0 << when interrupt enabled */
  uint8_t SEDint;
  uint8_t DERint;
  uint8_t CUL; /* Connected unit list */
  struct HDCshape specshape;

  /* Actual size of the drive as set in the config file */
//  struct HDCshape configshape[4];
};

/* The global Hard drive state structure */
//struct HDCStruct HDCData;
struct HDCStruct HDC;



#define BIT_BUSY (1<<7)
#define BIT_COMPARAMREJECT (1<<6)
#define BIT_COMEND (1<<5)
#define BIT_SEEKEND (1<<4)
#define BIT_DRIVEERR (1<<3)
#define BIT_ABNEND (1<<2)
#define BIT_POLL (1<<1)

/* Error codes */
#define ERR_ABT 0x04 /* Abort */
#define ERR_IVC 0x08 /* Invalid command */
#define ERR_PER 0x0c /* Parameter error */
#define ERR_NIN 0x10 /* Not initialized */
#define ERR_RTS 0x14 /* Rejected test (can't do test after specify) */
#define ERR_NRY 0x20 /* Drive not ready */
#define ERR_NSC 0x24 /* No SCP - i.e. seek screwed up */
#define ERR_INC 0x2c /* Invalid Next Cylinder Address */
#define ERR_IPH 0x3c /* Invalid PHA (Physical Head Address) */
#define ERR_DEE 0x40 /* DATA field ECC Error - or should I use the CRC one ? */
#define ERR_NHT 0x50 /* Not hit (data wrong in compare) */
#define ERR_TOV 0x58 /* Time Over */

#define PCVAL state->Reg[15]

/* Increasing this didn't help! */
#define REGULARTIME 250

/*
#define DEBUG_DMAWRITE
#define DEBUG_INTS
#define DEBUG
#define DEBUG_DATA 
*/
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  Interrupts (IRQ and DREQ are wire or'd) to IOC IL3 

  Its wired in 16 bit mode - so the status buffer is in the top byte

  Data in the files is organised in cylinder order.  Within each cylinder its
  organised in head order and then within each head there is each sector - same
  as the order of multiple sector reads.
*/

static void CheckData_DoNextBufferFull(ARMul_State *state);
static void ReadData_DoNextBufferFull(ARMul_State *state);
static void CompareData_DoNextBufferFull(ARMul_State *state);
static void WriteData_DoNextBufferFull(ARMul_State *state);
static void WriteFormat_DoNextBufferFull(ARMul_State *state);
static void Cause_Error(ARMul_State *state, int errcode);
static void ReturnParams(ARMul_State *state,const int NParams, ...);

/*---------------------------------------------------------------------------*/
/* Dump 256 bytes of data in hex dump format                                 */
#ifdef DEBUG_DATA
static void Dump256Block(uint8_t *Data) {
  int oindex,iindex;

  fputc('\n',stderr);

  for(oindex=0;oindex<256;oindex+=8) {
    fprintf(stderr,"0x%4x : ",oindex);

    for(iindex=0;iindex<8;iindex++) {
      fprintf(stderr,"%2x ",Data[oindex+iindex]);
    }

    for(iindex=0;iindex<8;iindex++) {
      if (isprint(Data[oindex+iindex])) {
        fputc(Data[oindex+iindex],stderr);
      } else {
        fputc('.',stderr);
      }
    }

    fputc('\n',stderr);
  } /* oindex */
  fputc('\n',stderr);
} /* Dump256Block */
#endif

/*---------------------------------------------------------------------------*/
uint32_t HDC_Regular(ARMul_State *state) {

  if (--HDC.DelayCount) {
    return(0);
  }

  switch (HDC.LastCommand) {
    case 0x40: /* Read data */
      if (HDC.DBufPtrs[HDC.CommandData.ReadData.NextDestBuffer ^1]>255) {
        ReadData_DoNextBufferFull(state);
        dbug_data("HDC_Regular: Read data buffer full case\n");
      } else {
        HDC.DelayCount=HDC.DelayLatch;
        dbug_data("HDC_Regular: Read data buffer not full case\n");
      }
      break;


    case 0x48: /* Check data */
      CheckData_DoNextBufferFull(state);
      break;

    case 0x87: /* Write data */
      if (HDC.DBufPtrs[HDC.CommandData.WriteData.CurrentSourceBuffer]>255) {
        WriteData_DoNextBufferFull(state);
        dbug_data("HDC_Regular: Write data buffer full case\n");
      } else {
        HDC.DelayCount=HDC.DelayLatch;
        dbug_data("HDC_Regular: Write data buffer not full case\n");
      }
      break;

    case 0x88: /* Compare data */
      /* Pinching writedata control for this */
      if (HDC.DBufPtrs[HDC.CommandData.WriteData.CurrentSourceBuffer]>255) {
        CompareData_DoNextBufferFull(state);
        fprintf(stderr,"HDC_Regular: Compare data buffer full case\n");
      } else {
        HDC.DelayCount=HDC.DelayLatch;
        fprintf(stderr,"HDC_Regular: Compare data buffer not full case\n");

        /* I think in the case of no data we are supposed to no hit - but I'm not sure - Hmm */
        HDC.DelayLatch=1024;
        HDC.SSB=0;
        ReturnParams(state,10,0,HDC.SSB,HDC.CommandData.WriteData.US,
                                        HDC.CommandData.WriteData.PHA,
                                        HDC.CommandData.WriteData.LCAH,
                                        HDC.CommandData.WriteData.LCAL,
                                        HDC.CommandData.WriteData.LHA,
                                        HDC.CommandData.WriteData.LSA,
                                        HDC.CommandData.WriteData.SCNTH,
                                        HDC.CommandData.WriteData.SCNTL); /* Probably should be updated */
        HDC.DREQ=false;
        Cause_Error(state,ERR_NHT);
      }
      break;

    case 0xa3: /* Write format */
      if (HDC.DBufPtrs[HDC.CommandData.WriteFormat.CurrentSourceBuffer]>255) {
        WriteFormat_DoNextBufferFull(state);
        fprintf(stderr,"HDC_Regulat: Write format buffer full case\n");
      } else {
        HDC.DelayCount=HDC.DelayLatch;
        fprintf(stderr,"HDC_Regular: Write format buffer not full case\n");
      }
      break;

    default:
      HDC.DelayCount=HDC.DelayLatch;
      break;
  } /* Command switch */

  return(0);
} /* HDC_Regular */

/*---------------------------------------------------------------------------*/
static void UpdateInterrupt(ARMul_State *state) {
  uint_fast8_t mask=(HDC.CEDint?0:BIT_COMEND) | (HDC.SEDint?0:BIT_SEEKEND) |
                    (HDC.DERint?0:BIT_DRIVEERR);

  dbug_ints("HDC-UpdateInterrupt mask=0x%x StatusReg=0x%x &=0x%x DREQ=%d\n",
                 mask,HDC.StatusReg,HDC.StatusReg & mask,HDC.DREQ);
  if ((HDC.StatusReg & mask) || HDC.DREQ) {
    ioc.IRQStatus |= IRQB_HDIRQ;
  } else {
    ioc.IRQStatus &= ~IRQB_HDIRQ;
  }
  IO_UpdateNirq(state);
} /* UpdateInterrupt */

/*---------------------------------------------------------------------------*/
static void Cause_CED(ARMul_State *state) {
  HDC.StatusReg|=BIT_COMEND;
  UpdateInterrupt(state);
  HDC.LastCommand=0xfff;
  HDC.StatusReg&=~BIT_BUSY;
} /* Cause_CED */

/*---------------------------------------------------------------------------*/
static void Clear_IntFlags(ARMul_State *state) {
  HDC.StatusReg&=~(BIT_COMEND | BIT_SEEKEND | BIT_DRIVEERR);
  UpdateInterrupt(state);
} /* Clear_IntFlags */

/*---------------------------------------------------------------------------*/
static void HDC_DMAWrite(ARMul_State *state, int offset, int data) {
  dbug_dmawrite("HDC DMA Write offset=0x%x data=0x%x r15=0x%x\n",offset,data,PCVAL);

  if (HDC.CurrentlyOpenDataBuffer==-1) {
    fprintf(stderr,"HDC_DMAWrite: DTR - to non-open data buffer\n");
    return;
  }

  if (HDC.DBufPtrs[HDC.CurrentlyOpenDataBuffer]>254) {
    fprintf(stderr,"HDC_DMAWrite: DTR - to data buffer %d - off end!\n",
            HDC.CurrentlyOpenDataBuffer);

    HDC.DREQ=false;
    UpdateInterrupt(state);
    return;
  } else {
    /* OK - lets transfer some data */
    int8_t db=HDC.CurrentlyOpenDataBuffer;
    uint_fast16_t off=HDC.DBufPtrs[db];

    HDC.DBufs[db][off++]=data & 0xff;
    HDC.DBufs[db][off++]=(data>>8) & 0xff;

    HDC.DBufPtrs[db]=off;

    dbug_dmawrite("HDC_DMAWrite: DTR to data buffer %d - got 0x%x\n",db,data);

    if (off>254) {
      /* Just finished off a block - lets fire the next one off - but lets drop
      DREQ for this one */

      HDC.DREQ=false;
      UpdateInterrupt(state);
      HDC.DelayLatch=HDC.DelayCount=5;
    } /* End of the buffer */
  } /* buffer not full at first */
} /* HDC_DMAWrite */

/*---------------------------------------------------------------------------*/
static ARMword HDC_DMARead(ARMul_State *state, int offset) {
  int tmpres;

  dbug_dmaread("HDC DMA Read offset=0x%x PCVAL=0x%x\n",offset,PCVAL);

  if (HDC.CurrentlyOpenDataBuffer==-1) {
    dbug("HDC_DMARead: DTR - from non-open data buffer\n");
    return(0x5aa5);
  }

  if (HDC.DBufPtrs[HDC.CurrentlyOpenDataBuffer]>254) {
    fprintf(stderr,"HDC_DMARead: DTR - from data buffer %d - off end!\n",
            HDC.CurrentlyOpenDataBuffer);
    HDC.DREQ=false;
    UpdateInterrupt(state);
    return(0x1223);
  } else {
    int8_t db=HDC.CurrentlyOpenDataBuffer;
    uint_fast16_t off=HDC.DBufPtrs[db];

    tmpres=HDC.DBufs[db][off++];
    tmpres|=(HDC.DBufs[db][off++]<<8);

    HDC.DBufPtrs[db]=off;

    dbug_dmaread("HDC_DMARead: DTR - from data buffer %d - returning 0x%x\n",db,(uint32_t)tmpres);

    if (off>254) {
      /* Ooh - just finished reading a block - lets fire the next one off
         - but nock down the request until it arrives */
      HDC.DREQ=false;
      UpdateInterrupt(state);
      HDC.DelayLatch=HDC.DelayCount=5;
    }
    return(tmpres);
  } /* Within buffer */
  return(0);
} /* HDC_DMARead */

/*---------------------------------------------------------------------------*/
static void PrintParams(ARMul_State *state) {
  uint32_t ptr;
  fprintf(stderr,"HDC: Param block contents: ");

  for(ptr=0;ptr<HDC.PBPtr;ptr++) {
    fprintf(stderr," 0x%x ",HDC.ParamBlock[ptr]); 
  }
  fprintf(stderr,"\n");
} /* PrintParams */

/*---------------------------------------------------------------------------*/
/* Place the following values in the parameter block to be read back by the host */
static void ReturnParams(ARMul_State *state,const int NParams, ...) {
  va_list args;
  int param;

  if (NParams>16) {
    fprintf(stderr,"HDC:ReturnParams - invalid number of parameters\n");
    abort();
  }

  va_start(args,NParams);

  for(param=0;param<NParams;param++) {
    HDC.ParamBlock[param]=va_arg(args,int);
  }

  HDC.PBPtr=0;
  va_end(args);
} /* ReturnParams */

/*---------------------------------------------------------------------------*/
/* Read the values out of the parameter block into the character size variables
   in the parameters.  Returns 1 if there were the parameters available - else
   0. */
static int GetParams(ARMul_State *state, const int NParams, ...) {
  va_list args;
  int param;
  uint8_t *resptr;

  if (NParams > 16) {
    fprintf(stderr,"HDC:GetParams - invalid number of parameters requested\n");
    abort();
  }

  if ((int)HDC.PBPtr < (NParams - 1)) {
    fprintf(stderr,"HDC:GetParams - not enough parameters passed to HDC (required=%d got=%d)\n",
            NParams,HDC.PBPtr);
    return(0);
  }

  if ((int)HDC.PBPtr > NParams) {
    dbug_data("HDC:GetParams - warning: Too many parameters passed to hdc\n");
  }

  va_start(args,NParams);

  for(param=0;param<NParams;param++) {
    resptr=va_arg(args,uint8_t *);
    *resptr=HDC.ParamBlock[param];
  }

  return(1);
} /* GetParams */

/*---------------------------------------------------------------------------*/
static void Cause_Error(ARMul_State *state, int errcode) {
  HDC.StatusReg|=BIT_ABNEND;
  HDC.SSB=errcode;
  Cause_CED(state);
} /* Cause_Error */

/*---------------------------------------------------------------------------*/

/* Sets the pointer in the appropriate data file - returns 1 if it succeeded */

/* FIXME: should have just one definition of this. */
#define DIM(a) ((sizeof (a)) / sizeof (a)[0])

static int SetFilePtr(ARMul_State *state, int drive, uint32_t head,
    uint32_t cyl, uint32_t sect)
{
    struct HDCshape *disc;
    uint32_t ptr;

    dbug("SetFilePtr: drive=%d head=%u cyl=%u sec=%u\n", drive, head, cyl, sect);

    if (drive < 0 || drive > DIM(hArcemConfig.aST506DiskShapes)) {
        ControlPane_Error(1,"SetFilePtr: drive %d out of range 0..%d\n",
            drive, (int) DIM(hArcemConfig.aST506DiskShapes));
    }

    disc = hArcemConfig.aST506DiskShapes + drive;
    dbug("SetFilePtr: disc: heads=%d sectors=%d sectorsize=%d\n",
        disc->NHeads, disc->NSectors, disc->RecordLength);

    if (cyl >= disc->NCyls) {
        dbug("cyl too large: %u >= %u\n", cyl, disc->NCyls);
        Cause_Error(state, ERR_NSC);
        return 0;
    }

    if (head >= disc->NHeads) {
        dbug("head too large: %u >= %u\n", head, disc->NHeads);
        /* I think this error is actually supposed to be used against
         * specified value not physical. */
        Cause_Error(state, ERR_IPH);
        return 0;
    }

    if (sect >= disc->NSectors) {
        dbug("sector too large: %u >= %u\n", sect, disc->NSectors);
        /* ID not found in time. */
        Cause_Error(state, ERR_TOV);
        return 0;
    }

    ptr = (((cyl * disc->NHeads) + head) * disc->NSectors + sect) *
        disc->RecordLength;
    dbug("SetFilePtr: ptr=%lu\n", ptr);

    if (!HDC.HardFile[drive]) {
        dbug("SetFilePtr:no image for drive %d\n", drive);
        Cause_Error(state, ERR_NRY);
        return 0;
    }

    if (fseek(HDC.HardFile[drive], ptr, SEEK_SET)) {
        dbug("SetFilePtr: fseek failed: errno=%d\n", errno);
        Cause_Error(state, ERR_NRY);
        return 0;
    }

    HDC.Track[drive] = cyl;

    return 1;
}

/*---------------------------------------------------------------------------*/
/* A new 'check drive' command has been received
   params in:
     US $00
   out:
     $00 SSB US $00 DST0 $00
*/
static void CheckDriveCommand(ARMul_State *state) {
  uint8_t Must00,US;

  dbug("HDC New command: Check drive\n");

  if (!GetParams(state,2,&Must00,&US)) {
    fprintf(stderr,"Check drive command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  if (Must00!=0) {
    fprintf(stderr,"Check drive - mandatory 0 missing (was=0x%x)\n",Must00);
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }
  dbug(" Check drive on US=%d\n",US);

  US&=3; /* only 2 bits of drive select */

  HDC.SSB=0;

  /* We get drive ready etc. from whether the image file opened! */
  if (HDC.HardFile[US]==NULL) {
    /*HDC.StatusReg|=BIT_DRIVEERR; - No it doesn't
    Cause_Error(state,ERR_NRY); */

    ReturnParams(state,6,0,HDC.SSB,US,0,0 /* DST */,0);
  } else {
    ReturnParams(state,6,0,HDC.SSB,US,0,(HDC.Track[US]==0)?0xe0:0xc0,0);
  } /* image file opened? */

  /* End of command */
  HDC.StatusReg&=~BIT_BUSY;
  UpdateInterrupt(state);

  HDC.LastCommand=0xfff; /* NOTE: Don't cause CED */

} /* CheckDriveCommand */
/*---------------------------------------------------------------------------*/
/* A new 'specify' command has just arrived                                  */
static void SpecifyCommand(ARMul_State *state) {
  uint8_t OM0,OM1,OM2,CUL,TONCH,NCL,NH,NS,SHRL,
                GPL1,GPL2,GPL3,LCCH,LCCL,PCCH,PCCL;

  dbug("HDC New command: Specify\n");

  /*kill(getpid(),SIGUSR2); *//* Turn on tracing ! */

  if (!GetParams(state,16,&OM0,&OM1,&OM2,&CUL,&TONCH,&NCL,&NH,&NS,&SHRL,
                 &GPL1,&GPL2,&GPL3,&LCCH,&LCCL,&PCCH,&PCCL)) {
    fprintf(stderr,"Specify command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  } /* SpecifyCommand */

  /* Print the values out in a structured format for debugging */

  dbug("HDC-Specify: OM0=%s,%s,%s,padp=%d,%s,crcp=%d,crci=%d acor=%d\n",
                               (OM0 & 128)?"hard sector":"soft  sector",
                               (OM0 & 64)?"NRZ":"Modified FM",
                               (OM0 & 32)?"SMD":"ST506",
                               (OM0 & 16)?1:0,
                               (OM0 & 8)?"ECC":"CRC",
                               (OM0 & 4)?1:0,
                               (OM0 & 2)?1:0,
                               (OM0 & 1));

  dbug("             OM1=%s,%s,CED=%s,SED=%s,DER=%s,AMEX=%d,%s\n",
                               (OM1 & 128)?"DMA":"PIO",
                               (OM1 & 64)?"Burst mode":"Cycle steal mode",
                               (OM1 & 32)?"no int":"int",
                               (OM1 & 16)?"no int":"int",
                               (OM1 & 8)?"no int":"int",
                               (OM1 & 2)?1:0,
                               (OM1 & 1)?"Parallel seek":"Normal seek");

  if (OM0 & 32) {
    fprintf(stderr,"HDC-Specify: SMD not supported\n");
    /* Make up an error code! */
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  dbug("            OM2=Step pulse low width=%d\n",OM2);
  dbug("            CUL=0x%x\n",CUL);
  dbug("            Read/write time over=%d\n",TONCH>>2);
  dbug("            Number of cylinders=%d\n",NCL | ((TONCH & 3)<<8));
  dbug("            Number of heads=%d\n",NH);
  dbug("            Number of sectors/track=%d\n",NS);
  dbug("            Step pulse high width=%d\n",SHRL>>3);
  dbug("            Record length code=%d\n",SHRL & 7);
  dbug("            GPL1..3=%d,%d,%d\n",GPL1,GPL2,GPL3);
  dbug("            Low current cylinder=%d\n",LCCL | (LCCH << 8));
  dbug("            Precompensation cylinder=%d\n",PCCL | (PCCH << 8));

  /* Lets figure out what all those parameters meant */
  HDC.dmaNpio=(OM1 & 128)?1:0;
  HDC.CEDint=(OM1 & 32)?1:0;
  HDC.SEDint=(OM1 & 16)?1:0;
  HDC.DERint=(OM1 & 8)?1:0;
  HDC.CUL=CUL;
  HDC.specshape.NCyls=(NCL | ((TONCH & 3)<<8))+1;
  HDC.specshape.NHeads=NH+1;
  HDC.specshape.NSectors=NS+1;

  switch (SHRL & 7) {
    case 0:
    case 6:
    case 7:
      fprintf(stderr,"HDC-Specify: Bad record size\n");
      HDC.specshape.RecordLength=1; /* Smaller than real sectors - won't cause an overrun anywhere! */
      break;

    case 1:
      HDC.specshape.RecordLength=256;
      break;

    case 2:
      HDC.specshape.RecordLength=512;
      break;

    case 3:
      HDC.specshape.RecordLength=1024;
      break;

    case 4:
      HDC.specshape.RecordLength=2048;
      break;

    case 5:
      HDC.specshape.RecordLength=4096;
      break;

  } /* Record size switch */
  /* OK - lets flag the fact that we have got a specify command - many other
     commands aren't allowed to execute until we set this */
  HDC.HaveGotSpecify=true;

  /* End of command */
  HDC.StatusReg&=~BIT_BUSY;

  HDC.LastCommand=0xfff; /* NOTE: Don't cause CED - Specify doesn't*/

  HDC.SSB=0; /* all peaceful */
  ReturnParams(state,2,0,HDC.SSB);
} /* SpecifyCommand */

/*---------------------------------------------------------------------------*/
/* Seek command
   In:
      US $00 NCAH NCAL

    Out:
      $00 SSB US VUL
*/
static void SeekCommand(ARMul_State *state) {
  uint8_t Must00,US,NCAH,NCAL;
  uint32_t DesiredCylinder;

  dbug("HDC New command: Seek\n");

  if (!HDC.HaveGotSpecify) {
    fprintf(stderr,"HDC - Seek - not had specify\n");
    Cause_Error(state,ERR_NIN);
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  if (!GetParams(state,4,&US,&Must00,&NCAH,&NCAL)) {
    fprintf(stderr,"Seek command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  if (Must00!=0) {
    fprintf(stderr,"Seek - mandatory 0 missing (was=0x%x)\n",Must00);
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  US&=3; /* only 2 bits of drive select */
  dbug("SeekCommand: drive %u\n", US);

  HDC.SSB=0;

  DesiredCylinder=NCAL | (NCAH << 8);

  if (DesiredCylinder>HDC.specshape.NCyls) {
    /* Ook - bad cylinder address */
        fprintf(stderr, "seek: cylinder address greater than "
            "specified, %u > %u\n", DesiredCylinder,
            HDC.specshape.NCyls);
    Cause_Error(state,ERR_INC); /* Invalid NCA */
    ReturnParams(state,4,0,HDC.SSB,US,0);
    return;
  }

  if (DesiredCylinder >= hArcemConfig.aST506DiskShapes[US].NCyls) {
    /* Ook - bad cylinder address */
        fprintf(stderr, "seek: cylinder address greater than "
            "or equal to configured, %u >= %u\n", DesiredCylinder,
            hArcemConfig.aST506DiskShapes[US].NCyls);
    Cause_Error(state,ERR_NSC); /* Seek screwed up */
    ReturnParams(state,4,0,HDC.SSB,US,0);
    return;
  }

  /* OK - move the head - very quickly at the moment :-) */
  HDC.Track[US]=DesiredCylinder;

  dbug("HDC:SeekCommand to cylinder %d on drive %d\n",DesiredCylinder,US);

  /* OK - return all fine */
  ReturnParams(state,4,0,HDC.SSB,US,HDC.CUL);
  HDC.StatusReg|=BIT_SEEKEND; /* as well as command end */
  Cause_CED(state);
} /* SeekCommand */

/*---------------------------------------------------------------------------*/
/* recalibrate command (p.323)
   In:
      US $00

    Out:
      $00 SSB US VUL
*/
static void RecalibrateCommand(ARMul_State *state) {
  uint8_t Must00,US;

  dbug("HDC New command: Recalibrate\n");

  if (!HDC.HaveGotSpecify) {
    fprintf(stderr,"HDC - Recalibrate - not had specify\n");
    Cause_Error(state,ERR_NIN);
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  if (!GetParams(state,2,&US,&Must00)) {
    fprintf(stderr,"Recalibrate command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  if (Must00!=0) {
    fprintf(stderr,"Recalibrate - mandatory 0 missing (was=0x%x)\n",Must00);
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,2,0,HDC.SSB);
    return;
  }

  US&=3; /* only 2 bits of drive select */

  HDC.SSB=0;

  /* OK - move the head - very quickly at the moment :-) */
  HDC.Track[US]=0;

  /* OK - return all fine */
  ReturnParams(state,4,0,HDC.SSB,US,HDC.CUL);
  HDC.StatusReg|=BIT_SEEKEND; /* as well as command end */
  Cause_CED(state);
} /* ReclibrateCommand */

/*---------------------------------------------------------------------------*/
/* Fills the buffer up for the read data command                             */
static void ReadData_DoNextBufferFull(ARMul_State *state) {
  /* First check the number of buffers left - if its 0 then its end of command */
  if (HDC.CommandData.ReadData.BuffersLeft==0) {
    /* End of command */
    Cause_CED(state);
    HDC.DelayLatch=1024;
    HDC.SSB=0;
    ReturnParams(state,10,0,HDC.SSB,
                 HDC.CommandData.ReadData.US,
                 HDC.CommandData.ReadData.PHA,
                 HDC.CommandData.ReadData.LCAH, /* Are these supposed to be updated addresses? */
                 HDC.CommandData.ReadData.LCAL,
                 HDC.CommandData.ReadData.LHA,
                 HDC.CommandData.ReadData.LSA,
                 HDC.CommandData.ReadData.SCNTH,
                 HDC.CommandData.ReadData.SCNTL);
    HDC.DREQ=false;
  } else {
    /* Fill her up! */
    HDC.CommandData.ReadData.BuffersLeft--;
    HDC.DREQ=true;
    UpdateInterrupt(state);

    fread(HDC.DBufs[HDC.CommandData.ReadData.NextDestBuffer],
          1,256,HDC.HardFile[HDC.CommandData.ReadData.US]);

    dbug_data("HDC:ReadData_DoNextBufferFull - just got\n");
#ifdef DEBUG_DATA
    Dump256Block(HDC.DBufs[HDC.CommandData.ReadData.NextDestBuffer]);
#endif

    HDC.DBufPtrs[HDC.CommandData.ReadData.NextDestBuffer]=0;
    HDC.CurrentlyOpenDataBuffer=HDC.CommandData.ReadData.NextDestBuffer;
    HDC.CommandData.ReadData.NextDestBuffer^=1;
  } /* End ? */
} /* ReadData_DoNextBufferFull */

/*---------------------------------------------------------------------------*/
/* in
  US PHA LCAH LCAL LHA LSA SCNTH SCNTL
  out
  $00 SSB US PHA LCAH LCAL LHA LSA SCNTH SCNTL */
static void ReadDataCommand(ARMul_State *state) {
  uint8_t US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL;
#ifdef BENCHMARKEXIT
  static int exitcount=0;
#endif

  dbug("HDC New command: Read data\n");

#ifdef BENCHMARKEXIT
  /* TMP for timing!! */
  exitcount++;
  if (exitcount>140) {
    printf("Emulated cycles=%ld\n",ARMul_Time);
    exit(0);
  }
#endif

  if (!HDC.HaveGotSpecify) {
    fprintf(stderr,"HDC - Read data - not had specify\n");
    Cause_Error(state,ERR_NIN);
    ReturnParams(state,2,0,HDC.SSB);
    Cause_CED(state);
    return;
  }

  if (!GetParams(state,8,&US,&PHA,&LCAH,&LCAL,&LHA,&LSA,&SCNTH,&SCNTL)) {
    fprintf(stderr,"Read data command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    Cause_CED(state);
    return;
  }

  US&=3; /* only 2 bits of drive select */

  HDC.SSB=0;

  HDC.CommandData.ReadData.US=US;
  HDC.CommandData.ReadData.PHA=PHA;
  HDC.CommandData.ReadData.LCAH=LCAH;
  HDC.CommandData.ReadData.LCAL=LCAL;
  HDC.CommandData.ReadData.LHA=LHA; /* Logical head address */
  HDC.CommandData.ReadData.LSA=LSA; /* Logical sector address */
  HDC.CommandData.ReadData.SCNTH=SCNTH;
  HDC.CommandData.ReadData.SCNTL=SCNTL;

  dbug("HDC:ReadData command: US=%d PHA=%d LCA=%d LDA=%d LSA=%d SCNT=%d\n",
          US,PHA,LCAL | (LCAH<<8),LHA,LSA,SCNTL | (SCNTH<<8));

  /* Couldn't set file ptr - off the end? */
  if (!SetFilePtr(state,US,PHA,LCAL | (LCAH<<8),LSA)) {
    fprintf(stderr,"Read data command exiting due to failed file seek\n");
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    HDC.StatusReg|=BIT_DRIVEERR;
    Cause_CED(state);
    return;
  }

  /* Number of buffers to read */
  HDC.CommandData.ReadData.BuffersLeft    = ((SCNTL+(SCNTH<<8)) * hArcemConfig.aST506DiskShapes[US].RecordLength) / 256;
  HDC.CommandData.ReadData.NextDestBuffer = 0;
  HDC.CurrentlyOpenDataBuffer=0;

  HDC.DelayCount=1;
  HDC.DelayLatch=5;
  HDC.DBufPtrs[0]=HDC.DBufPtrs[1]=256; /* So that the fill routine presumes the previous buffer has finished reading */
  HDC.StatusReg|=BIT_BUSY;
} /* ReadDataCommand */

/*---------------------------------------------------------------------------*/
/* in
  US PHA LCAH LCAL LHA LSA SCNTH SCNTL
  out
  $00 SSB US PHA LCAH LCAL LHA LSA SCNTH SCNTL */
static void WriteDataCommand(ARMul_State *state) {
  uint8_t US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL;

  dbug("HDC New command: Write data\n");

  if (!HDC.HaveGotSpecify) {
    fprintf(stderr,"HDC - Write data - not had specify\n");
    Cause_Error(state,ERR_NIN);
    ReturnParams(state,2,0,HDC.SSB);
    Cause_CED(state);
    return;
  }

  if (!GetParams(state,8,&US,&PHA,&LCAH,&LCAL,&LHA,&LSA,&SCNTH,&SCNTL)) {
    fprintf(stderr,"Write data command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    Cause_CED(state);
    return;
  }

  US&=3; /* only 2 bits of drive select */

  HDC.SSB=0;

  HDC.CommandData.WriteData.US=US;
  HDC.CommandData.WriteData.PHA=PHA;
  HDC.CommandData.WriteData.LCAH=LCAH;
  HDC.CommandData.WriteData.LCAL=LCAL;
  HDC.CommandData.WriteData.LHA=LHA; /* Logical head address */
  HDC.CommandData.WriteData.LSA=LSA; /* Logical sector address */
  HDC.CommandData.WriteData.SCNTH=SCNTH;
  HDC.CommandData.WriteData.SCNTL=SCNTL;

  dbug("HDC:WriteData command: US=%d PHA=%d LCA=%d LDA=%d LSA=%d SCNT=%d\n",
          US,PHA,LCAL | (LCAH<<8),LHA,LSA,SCNTL | (SCNTH<<8));

  /* Couldn't set file ptr - off the end? */
  if (!SetFilePtr(state,US,PHA,LCAL | (LCAH<<8),LSA)) {
    fprintf(stderr,"Write data command exiting due to failed file seek\n");
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    HDC.StatusReg|=BIT_DRIVEERR;
    Cause_CED(state);
    return;
  }

  /* Number of buffers to Write */
  HDC.CommandData.WriteData.BuffersLeft=((SCNTL+(SCNTH<<8))* hArcemConfig.aST506DiskShapes[US].RecordLength)/256;
  HDC.CommandData.WriteData.CurrentSourceBuffer=0;
  HDC.CurrentlyOpenDataBuffer=0;

  HDC.DelayCount=1;
  HDC.DelayLatch=5;
  HDC.DBufPtrs[0]=HDC.DBufPtrs[1]=0; /* So that the fill routine presumes the previous buffer has finished reading */
  HDC.StatusReg|=BIT_BUSY;
  HDC.DREQ=true; /* Request some data */
  UpdateInterrupt(state);
} /* WriteDataCommand */

/*---------------------------------------------------------------------------*/
/* The host has kindly provided enough data to fill an entire DMA buffer     */
static void WriteData_DoNextBufferFull(ARMul_State *state) {
  /* OK - lets store the current buffer full - or until we run out of sectors
  to do */

  HDC.CurrentlyOpenDataBuffer^=1;
  HDC.DBufPtrs[HDC.CurrentlyOpenDataBuffer]=0;

  dbug_data("HDC:WriteData_DoNextBufferFull - about to throw the following to disc:\n");
#ifdef DEBUG_DATA
  Dump256Block(HDC.DBufs[HDC.CommandData.WriteData.CurrentSourceBuffer]);
#endif

  /* Throw the data out to the disc */
  fwrite(HDC.DBufs[HDC.CommandData.WriteData.CurrentSourceBuffer],1,256,
         HDC.HardFile[HDC.CommandData.WriteData.US]);
  fflush(HDC.HardFile[HDC.CommandData.WriteData.US]);

  HDC.CommandData.WriteData.CurrentSourceBuffer^=1;
  HDC.CommandData.WriteData.BuffersLeft--;

  /* If there are more sectors left then lets trigger off another DMA else end */
  if (HDC.CommandData.WriteData.BuffersLeft>0) {
    HDC.DREQ=true;
    UpdateInterrupt(state);
  } else {
    Cause_CED(state);
    HDC.DelayLatch=1024;
    HDC.SSB=0;
    ReturnParams(state,10,0,HDC.SSB,HDC.CommandData.WriteData.US,
                                   HDC.CommandData.WriteData.PHA,
                                   HDC.CommandData.WriteData.LCAH,
                                   HDC.CommandData.WriteData.LCAL,
                                   HDC.CommandData.WriteData.LHA,
                                   HDC.CommandData.WriteData.LSA,
                                   HDC.CommandData.WriteData.SCNTH,
                                   HDC.CommandData.WriteData.SCNTL); /* Probably should be updated */
    HDC.DREQ=false;
  }
} /* WriteData_DoNextBufferFull */

/*---------------------------------------------------------------------------*/
/* in
  US PHA LCAH LCAL LHA LSA SCNTH SCNTL
  out
  $00 SSB US PHA LCAH LCAL LHA LSA SCNTH SCNTL */
static void CompareDataCommand(ARMul_State *state) {
  uint8_t US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL;

  dbug("HDC New command: Compare data\n");

  if (!HDC.HaveGotSpecify) {
    fprintf(stderr,"HDC - Compare data - not had specify\n");
    Cause_Error(state,ERR_NIN);
    ReturnParams(state,2,0,HDC.SSB);
    Cause_CED(state);
    return;
  }

  if (!GetParams(state,8,&US,&PHA,&LCAH,&LCAL,&LHA,&LSA,&SCNTH,&SCNTL)) {
    fprintf(stderr,"Compare data command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    Cause_CED(state);
    return;
  }

  US&=3; /* only 2 bits of drive select */

  HDC.SSB=0;

  HDC.CommandData.WriteData.US=US;
  HDC.CommandData.WriteData.PHA=PHA;
  HDC.CommandData.WriteData.LCAH=LCAH;
  HDC.CommandData.WriteData.LCAL=LCAL;
  HDC.CommandData.WriteData.LHA=LHA; /* Logical head address */
  HDC.CommandData.WriteData.LSA=LSA; /* Logical sector address */
  HDC.CommandData.WriteData.SCNTH=SCNTH;
  HDC.CommandData.WriteData.SCNTL=SCNTL;

  fprintf(stderr,"HDC:CompareData command: US=%d PHA=%d LCA=%d LDA=%d LSA=%d SCNT=%d\n",
          US,PHA,LCAL | (LCAH<<8),LHA,LSA,SCNTL | (SCNTH<<8));

  /* Couldn't set file ptr - off the end? */
  if (!SetFilePtr(state,US,PHA,LCAL | (LCAH<<8),LSA)) {
    fprintf(stderr,"Compare data command exiting due to failed file seek\n");
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    HDC.StatusReg|=BIT_DRIVEERR;
    Cause_CED(state);
    return;
  }

  /* Number of buffers to compare */
  HDC.CommandData.WriteData.BuffersLeft=((SCNTL+(SCNTH<<8)) * hArcemConfig.aST506DiskShapes[US].RecordLength)/256;
  HDC.CommandData.WriteData.CurrentSourceBuffer=0;
  HDC.CurrentlyOpenDataBuffer=0;

  HDC.DelayCount=1;
  HDC.DelayLatch=5;
  HDC.DBufPtrs[0]=HDC.DBufPtrs[1]=0; /* So that the fill routine presumes the previous buffer has finished reading */
  HDC.StatusReg|=BIT_BUSY;
  HDC.DREQ=true; /* Request some data */
  UpdateInterrupt(state);
} /* CompareDataCommand */

/*---------------------------------------------------------------------------*/
/* The host has kindly provided enough data to fill an entire DMA buffer     */
static void CompareData_DoNextBufferFull(ARMul_State *state) {
  uint8_t tmpbuf[256];

  HDC.CurrentlyOpenDataBuffer^=1;
  HDC.DBufPtrs[HDC.CurrentlyOpenDataBuffer]=0;

  dbug_data("HDC:CompareData_DoNextBufferFull - about to check the following against the disc:\n");
#ifdef DEBUG_DATA
  Dump256Block(HDC.DBufs[HDC.CommandData.WriteData.CurrentSourceBuffer]);
#endif

  /* Throw the data out to the disc */
  fread(tmpbuf,1,256,HDC.HardFile[HDC.CommandData.WriteData.US]);

  if (memcmp(tmpbuf,HDC.DBufs[HDC.CommandData.WriteData.CurrentSourceBuffer],256)!=0) {
    /* Oops - data didn't compare */
    Cause_CED(state);
    HDC.DelayLatch=1024;
    HDC.SSB=0;
    ReturnParams(state,10,0,HDC.SSB,HDC.CommandData.WriteData.US,
                                    HDC.CommandData.WriteData.PHA,
                                    HDC.CommandData.WriteData.LCAH,
                                    HDC.CommandData.WriteData.LCAL,
                                    HDC.CommandData.WriteData.LHA,
                                    HDC.CommandData.WriteData.LSA,
                                    HDC.CommandData.WriteData.SCNTH,
                                    HDC.CommandData.WriteData.SCNTL); /* Probably should be updated */
    HDC.DREQ=false;
    Cause_Error(state,ERR_NHT);
    return;
  }

  HDC.CommandData.WriteData.CurrentSourceBuffer^=1;
  HDC.CommandData.WriteData.BuffersLeft--;

  /* If there are more sectors left then lets trigger off another DMA else end */
  if (HDC.CommandData.WriteData.BuffersLeft>0) {
    HDC.DREQ=true;
    UpdateInterrupt(state);
  } else {
    Cause_CED(state);
    HDC.DelayLatch=1024;
    HDC.SSB=0;
    ReturnParams(state,10,0,HDC.SSB,HDC.CommandData.WriteData.US,
                                    HDC.CommandData.WriteData.PHA,
                                    HDC.CommandData.WriteData.LCAH,
                                    HDC.CommandData.WriteData.LCAL,
                                    HDC.CommandData.WriteData.LHA,
                                    HDC.CommandData.WriteData.LSA,
                                    HDC.CommandData.WriteData.SCNTH,
                                    HDC.CommandData.WriteData.SCNTL); /* Probably should be updated */
    HDC.DREQ=false;
  }
} /* CompareData_DoNextBufferFull */

/*---------------------------------------------------------------------------*/
/* Reads the data for a CheckData command - and returns a fault if there is
   a problem */
static void CheckData_DoNextBufferFull(ARMul_State *state) {
  /* First check the number of buffers left - if its 0 then its end of command */
  if (HDC.CommandData.ReadData.BuffersLeft==0) {
    /* End of command */
    Cause_CED(state);
    HDC.DelayLatch=1024;
    HDC.SSB=0;
    ReturnParams(state,10,0,HDC.SSB,
                 HDC.CommandData.ReadData.US,
                 HDC.CommandData.ReadData.PHA,
                 HDC.CommandData.ReadData.LCAH, /* Are these supposed to be updated addresses? */
                 HDC.CommandData.ReadData.LCAL,
                 HDC.CommandData.ReadData.LHA,
                 HDC.CommandData.ReadData.LSA,
                 HDC.CommandData.ReadData.SCNTH,
                 HDC.CommandData.ReadData.SCNTL);
  } else {
    static char tmpbuff[256];
    int retval;

    /* Fill here up! */
    if (retval=fread(tmpbuff,1,256,HDC.HardFile[HDC.CommandData.ReadData.US]),retval!=256)
    {
      fprintf(stderr,"HDC: CheckData_DoNextBufferFull - returning data err - retval=0x%x\n",retval);
      /* End of command */
      Cause_CED(state);
      HDC.DelayLatch=1024;
      Cause_Error(state,ERR_DEE); /* Data error */
      ReturnParams(state,10,0,HDC.SSB,
                   HDC.CommandData.ReadData.US,
                   HDC.CommandData.ReadData.PHA,
                   HDC.CommandData.ReadData.LCAH, /* Are these supposed to be updated addresses? */
                   HDC.CommandData.ReadData.LCAL,
                   HDC.CommandData.ReadData.LHA,
                   HDC.CommandData.ReadData.LSA,
                   HDC.CommandData.ReadData.SCNTH,
                   HDC.CommandData.ReadData.SCNTL);
    }
    HDC.CommandData.ReadData.BuffersLeft--;
    UpdateInterrupt(state);

  } /* End ? */
  HDC.DelayCount=HDC.DelayLatch;
} /* CheckData_DoNextBufferFull */

/*---------------------------------------------------------------------------*/
/* This code is basically identical to the readdata command except that the
   data is never actually passed to the host - but we do check we can
   read it from the image file - just in case!

   We even use the ReadData commanddata section (ahem!)
   */
/* in
  US PHA LCAH LCAL LHA LSA SCNTH SCNTL
  out
  $00 SSB US PHA LCAH LCAL LHA LSA SCNTH SCNTL */
static void CheckDataCommand(ARMul_State *state) {
  uint8_t US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL;

  dbug("HDC New command: Check data\n");

  if (!HDC.HaveGotSpecify) {
    fprintf(stderr,"HDC - Check data - not had specify\n");
    Cause_Error(state,ERR_NIN);
    ReturnParams(state,2,0,HDC.SSB);
    Cause_CED(state);
    return;
  }

  if (!GetParams(state,8,&US,&PHA,&LCAH,&LCAL,&LHA,&LSA,&SCNTH,&SCNTL)) {
    fprintf(stderr,"Check data command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    Cause_CED(state);
    return;
  }

  US&=3; /* only 2 bits of drive select */

  HDC.SSB=0;

  HDC.CommandData.ReadData.US=US;
  HDC.CommandData.ReadData.PHA=PHA;
  HDC.CommandData.ReadData.LCAH=LCAH;
  HDC.CommandData.ReadData.LCAL=LCAL;
  HDC.CommandData.ReadData.LHA=LHA; /* Logical head address */
  HDC.CommandData.ReadData.LSA=LSA; /* Logical sector address */
  HDC.CommandData.ReadData.SCNTH=SCNTH;
  HDC.CommandData.ReadData.SCNTL=SCNTL;

  dbug("HDC:CheckData command: US=%d PHA=%d LCA=%d LDA=%d LSA=%d SCNT=%d\n",
          US,PHA,LCAL | (LCAH<<8),LHA,LSA,SCNTL | (SCNTH<<8));

  /* Couldn't set file ptr - off the end? */
  if (!SetFilePtr(state,US,PHA,LCAL | (LCAH<<8),LSA)) {
    fprintf(stderr,"Check data command exiting due to failed file seek\n");
    ReturnParams(state,10,0,HDC.SSB,US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL);
    HDC.StatusReg|=BIT_DRIVEERR;
    Cause_CED(state);
    return;
  }

  /* Number of buffers to read */
  HDC.CommandData.ReadData.BuffersLeft=((SCNTL+(SCNTH<<8)) * hArcemConfig.aST506DiskShapes[US].RecordLength)/256;
  HDC.CommandData.ReadData.NextDestBuffer=0;

  HDC.DelayCount=1;
  HDC.DelayLatch=5;
  HDC.StatusReg|=BIT_BUSY;
} /* CheckDataCommand */

/*---------------------------------------------------------------------------*/
/* Write format command (p.349)
   in
      US, PHA, SCNTH, SCNTL

    result param
      $00,SSB,US,PHA,SCNTH,SCNTL

*/
static void WriteFormatCommand(ARMul_State *state) {
  uint8_t US,PHA,SCNTH,SCNTL;

  dbug("HDC New command: Write format\n");

  if (!HDC.HaveGotSpecify) {
    fprintf(stderr,"HDC - write format - not had specify\n");
    Cause_Error(state,ERR_NIN);
    ReturnParams(state,2,0,HDC.SSB);
    Cause_CED(state);
    return;
  }

  if (!GetParams(state,4,&US,&PHA,&SCNTH,&SCNTL)) {
    fprintf(stderr,"Write format command exiting due to insufficient parameters\n");
    Cause_Error(state,ERR_PER); /* parameter error */
    ReturnParams(state,6,0,HDC.SSB,US,PHA,SCNTH,SCNTL);
    Cause_CED(state);
    return;
  }

  US&=3; /* only 2 bits of drive select */

  HDC.SSB=0;

  HDC.CommandData.WriteFormat.US=US;
  HDC.CommandData.WriteFormat.PHA=PHA;
  HDC.CommandData.WriteFormat.SCNTH=SCNTH;
  HDC.CommandData.WriteFormat.SCNTL=SCNTL;

  dbug("HDC:WriteFormat command: US=%d PHA=%d SCNT=%d\n",US,PHA,SCNTL | (SCNTH<<8));

  /* Couldn't set file ptr - hmm - I hope this doesn't cause problems for formatting arbitary parts of the disc */
  if (!SetFilePtr(state,US,PHA,HDC.Track[US],0)) {
    fprintf(stderr,"Write format command exiting due to failed file seek\n");
    ReturnParams(state,6,0,HDC.SSB,US,PHA,SCNTH,SCNTL);
    HDC.StatusReg|=BIT_DRIVEERR;
    Cause_CED(state);
    return;
  }

  HDC.CommandData.WriteFormat.SectorsLeft=SCNTL | (SCNTH<<8);
  HDC.CommandData.WriteFormat.CurrentSourceBuffer=0;
  HDC.CurrentlyOpenDataBuffer=0;

  HDC.DelayCount=1;
  HDC.DelayLatch=5;
  HDC.DBufPtrs[0]=HDC.DBufPtrs[1]=0; /* Nothing received from the host yet */
  HDC.StatusReg|=BIT_BUSY;
  HDC.DREQ=true; /* Request some data */
  UpdateInterrupt(state);
} /* WriteFormatCommand */

/*---------------------------------------------------------------------------*/
/* The host has kindly provided enough ID fields to fill an entire DMA buffer*/
static void WriteFormat_DoNextBufferFull(ARMul_State *state) {
  /* OK - lets parse the current buffer full - or until we run out of sectors
  to do */
  int ptr=0;
  int sectorsleft=HDC.CommandData.WriteFormat.SectorsLeft;
  char *fillbuffer; /* A buffer which holds a block of data to write */

  if (fillbuffer=malloc(8192),fillbuffer==NULL) {
    ControlPane_Error(1,"HDC:WriteFormat_DoNextBufferFull: Couldn't allocate memory for fillbuffer\n");
  }

  memset(fillbuffer,0x4e,8192);

  HDC.CurrentlyOpenDataBuffer^=1;
  HDC.DBufPtrs[HDC.CurrentlyOpenDataBuffer]=0;

  while ((ptr<255) && (sectorsleft--)) {
    /* The ID information is actually irrelevant to us - we aren't going to allow
    physical and logical cylinders to mismatch - if we are then we are in trouble! */

    /* Write the block to the hard disc image file */
    fwrite(fillbuffer, 1, hArcemConfig.aST506DiskShapes[HDC.CommandData.WriteFormat.US].RecordLength,
           HDC.HardFile[HDC.CommandData.WriteFormat.US]);
    fflush(HDC.HardFile[HDC.CommandData.WriteFormat.US]);

    ptr+=4; /* 4 bytes of values taken out of the buffer */
  } /* Sector loop */

  HDC.CommandData.WriteFormat.SectorsLeft=sectorsleft;

  free(fillbuffer);

  /* If there are more sectors left then lets trigger off another DMA else end */
  if (sectorsleft>0) {
    HDC.DREQ=true;
    UpdateInterrupt(state);
  } else {
    Cause_CED(state);
    HDC.DelayLatch=1024;
    HDC.SSB=0;
    ReturnParams(state,6,0,HDC.SSB,HDC.CommandData.WriteFormat.US,
                                   HDC.CommandData.WriteFormat.PHA,
                                   HDC.CommandData.WriteFormat.SCNTH,
                                   HDC.CommandData.WriteFormat.SCNTL); /* Probably should be updated */
    HDC.DREQ=false;
  }
} /* WriteFormat_DoNextBufferFull */

/*---------------------------------------------------------------------------*/
static void HDC_StartNewCommand(ARMul_State *state, int data) {
  if ((data & 0xf0)==0xf0) {
    /* Abort command */
    dbug("HDC: Abort command\n");
    Clear_IntFlags(state);
    Cause_CED(state); 
    /*HDC.StatusReg|=BIT_BUSY; */
    HDC.StatusReg&=~BIT_BUSY; /* Looks like this should cause a command to end
                                - but shouldn't be busy for any length of time
                                - perhaps I should make it go high and hten drop off */
    /* Place values in parameter block - 0 and then error 4 - command aborted */
    ReturnParams(state,2,0,ERR_ABT);
    return;
  } /* Abort */

  /* 'recall */
  if (data==0x8) {
    dbug("HDC: Recall command\n");
    HDC.LastCommand=-1;
    HDC.PBPtr=0;
    HDC.StatusReg&=~(BIT_BUSY | BIT_ABNEND | BIT_COMPARAMREJECT);
    Clear_IntFlags(state);
    return;
  } /* Recall */

  if (HDC.LastCommand!=-1) {
    /*fprintf(stderr,"HDC New command received (0x%x) but not in idle state (last command=0x%x)\n",
       data,HDC.LastCommand); */
    /* Hmm - this seems to happen - I expected recalls to always be used */
    HDC.LastCommand=-1;
    HDC.StatusReg&=~(BIT_BUSY | BIT_ABNEND);
    Clear_IntFlags(state);
  }

  HDC.SSB=0;
  HDC.LastCommand=data;

  HDC.StatusReg|=BIT_COMPARAMREJECT;

  switch (data) {
    case 0x10:
      dbug("HDC enable polling - !!!!!not supported!!!!!\n");
      HDC.StatusReg&=~BIT_COMPARAMREJECT;
      break;

    case 0x18:
      dbug("HDC disable polling - !!!!!not supported!!!!!\n");
      HDC.StatusReg&=~BIT_COMPARAMREJECT;
      break;

    case 0x28:
      CheckDriveCommand(state);
      break;

    case 0x40:
      ReadDataCommand(state);
      break;

    case 0x48:
      CheckDataCommand(state);
      break;

    case 0x87:
      WriteDataCommand(state);
      break;

    case 0x88:
      CompareDataCommand(state);
      break;

    case 0xa3:
      WriteFormatCommand(state);
      break;

    case 0xc0:
      SeekCommand(state);
      break;

    case 0xc8:
      RecalibrateCommand(state);
      break;

    case 0xe8:
      SpecifyCommand(state);
      break;

    default:
      fprintf(stderr,"-------------- HDC New command: Data=0x%x - unknown/unimplemented PC=0x%x\n",data,(uint32_t)(PCVAL));
      HDC.StatusReg&=~BIT_COMPARAMREJECT;
      PrintParams(state);
      abort();
      break;
  } /* Command switch */
} /* HDC_StartNewCommand */
/*---------------------------------------------------------------------------*/
/* Write to HDC memory space                                                 */
void HDC_Write(ARMul_State *state, int offset, int data) {
  bool rs=offset & 4;
  bool rNw=offset & 32;

  dbug("HDC_Write: offset=0x%x data=0x%x\n",offset,data);
  if (offset & 0x8) {
    HDC_DMAWrite(state,offset,data);
  } else {
    if (rNw) {
      fprintf(stderr,"HDC_Write: Write but address means read!!!!\n");
      return;
    }

    if (rs) {
      /* I reckon that if its in the command execution complete state
         we should return stuff from the parameter block otherwise
         we should return stuff from the data buffers  - possibly Idle as well */
      if ((HDC.LastCommand=0xfff) || (HDC.LastCommand==-1)) {
        /*fprintf(stderr,"HDC_Write: DTR data=0x%x\n",data); */
        if (!(HDC.StatusReg & BIT_COMPARAMREJECT)) {
          if (HDC.PBPtr<15) {
            /*fprintf(stderr,"HDC_Write: DTR to param block\n"); */
            HDC.ParamBlock[HDC.PBPtr++]=(data & 0xff00)>>8;
            HDC.ParamBlock[HDC.PBPtr++]=data & 0xff;
            if (HDC.PBPtr>=16) HDC.StatusReg|=BIT_COMPARAMREJECT;
          } /* 16 parameters */
        } /* Not rejecting parameters */
        return;
      } else {
        dbug("HDC Write offset=0x%x data=0x%x\n",offset,data);

        if (HDC.CurrentlyOpenDataBuffer==-1) {
          fprintf(stderr,"HDC_Write: DTR - to non-open data buffer\n");
          return;
        }

        if (HDC.DBufPtrs[HDC.CurrentlyOpenDataBuffer]>254) {
          fprintf(stderr,"HDC_Write: DTR - to data buffer %d - off end!\n",
                  HDC.CurrentlyOpenDataBuffer);

          HDC.DREQ=false;
          UpdateInterrupt(state);
          return;
        } else {
          /* OK - lets transfer some data */
          int8_t db=HDC.CurrentlyOpenDataBuffer;
          uint_fast16_t off=HDC.DBufPtrs[db];

          HDC.DBufs[db][off++]=data & 0xff;
          HDC.DBufs[db][off++]=(data>>8) & 0xff;

          HDC.DBufPtrs[db]=off;

          dbug("HDC_Write: DTR to data buffer %d - got 0x%x\n",db,data);

          if (off>254) {
            /* Just finished off a block - lets fire the next one off - but lets drop
            DREQ for this one */

            HDC.DREQ=false;
            UpdateInterrupt(state);
            HDC.DelayLatch=HDC.DelayCount=5;
          } /* End of the buffer */
        } /* buffer not full at first */
      } /* write data */
    } else {
      dbug("HDC_Write: Command reg = 0x%x pc=0x%x\n",data,(uint32_t)(PCVAL));
      if (!(HDC.StatusReg & BIT_BUSY)) {
        HDC_StartNewCommand(state,data);
      }
      HDC.LastCommand=data;
      return;
    }
  } /* DMA or normal ? */
} /* HDC_Write */

/*---------------------------------------------------------------------------*/
/* Read from HDC memory space                                                */
ARMword HDC_Read(ARMul_State *state, int offset) {
  bool rs=offset & 4;
  bool rNw=offset & 32;

  dbug("HDC_Read: offset=0x%x\n",offset);

  if (offset & 0x8) {
    return(HDC_DMARead(state,offset));
  }

  /* Read of HDC registers */
  if (!rNw) {
    dbug("HDC_Read: Read but address means write\n");
    return(0xff);
  }

  if (rs) {
    ARMword tmpres;
    /* I reckon that if its in the command execution complete state
       we should return stuff from the parameter block otherwise
       we should return stuff from the data buffers  - possibly Idle as well */
    if ((HDC.LastCommand=0xfff) || (HDC.LastCommand==-1)) {
      /* OK - its the parameter buffer */
      if (HDC.PBPtr<15) {
        tmpres=HDC.ParamBlock[HDC.PBPtr++];
        tmpres<<=8; /* This is presuming big endian - as the 68K for which this
                       beast is designed - yuk! */
        tmpres|=HDC.ParamBlock[HDC.PBPtr++];
        /*fprintf(stderr,"HDC_Read: DTR - from param buffer - 0x%x\n",tmpres);*/
        return(tmpres);
      } else {
        dbug("HDC_Read: DTR - from parameter buffer - but read past end!\n");
        return(0xa55a);
      }
    } else {
      /* One of the data buffers */
      if (HDC.CurrentlyOpenDataBuffer==-1) {
        dbug("HDC_Read: DTR - from non-open data buffer\n");
        return(0x5a);
      }

      if (HDC.DBufPtrs[HDC.CurrentlyOpenDataBuffer]>254) {
        dbug("HDC_Read: DTR - from data buffer %d - off end!\n",
                HDC.CurrentlyOpenDataBuffer);
        HDC.DREQ=false;
        UpdateInterrupt(state);
        return(0x1223);
      } else {
        int8_t db=HDC.CurrentlyOpenDataBuffer;
        uint_fast16_t off=HDC.DBufPtrs[db];

        tmpres=HDC.DBufs[db][off++];
        tmpres|=(HDC.DBufs[db][off++]<<8);

        HDC.DBufPtrs[db]=off;

        /*fprintf(stderr,"HDC_Read: DTR - from data buffer %d - returning 0x%x\n",db,(uint32_t)tmpres); */

        if (off>254) {
          /* Ooh - just finished reading a block - lets fire the next one off
             - but nock down the request until it arrives */
          HDC.DREQ=false;
          UpdateInterrupt(state);
          HDC.DelayLatch=HDC.DelayCount=5;
        }
        return(tmpres);
      } /* Within buffer */
    } /* Data buffer or Param buffer ? */
  } else {
    /* Should I copy the command register in here? */
    dbug("HDC_Read: Status (=0x%x) (PC=0x%x)\n",HDC.StatusReg<<8,(uint32_t)(PCVAL));
    return(HDC.StatusReg<<8);
  }
} /* HDC_Read */

/*---------------------------------------------------------------------------*/
void HDC_Init(ARMul_State *state) {
  int currentdrive;
  char FileName[32];
  
  /* initialise the struct as if it had been calloc'd */
  memset(&HDC, 0, sizeof(struct HDCStruct));
  
  
  HDC.StatusReg=0;
  HDC.Sector=0;
  HDC.PBPtr=HDC.DBufPtrs[0]=HDC.DBufPtrs[1]=0;
  HDC.LastCommand=-1;
  HDC.CurrentlyOpenDataBuffer=-1;
  HDC.HaveGotSpecify=false;
  HDC.SSB=0;
  HDC.CEDint=HDC.SEDint=HDC.DERint=0;

  for (currentdrive = 0; currentdrive < 4; currentdrive++) {
    HDC.Track[currentdrive] = 0;
      
#ifndef MACOSX
#ifdef __riscos__
    sprintf(FileName, "<ArcEm$Dir>.HardImage%d", currentdrive);
#else
    sprintf(FileName, "HardImage%d", currentdrive);
#endif
#else
    sprintf(FileName, "%s/arcem/HardImage%d", getenv("HOME"), currentdrive);
#endif

    {
      FILE *isThere = fopen(FileName, "rb");

      if (isThere) {
        fclose(isThere);
        HDC.HardFile[currentdrive] = fopen(FileName,"rb+");
      } else {
        HDC.HardFile[currentdrive] = NULL;
      }
    }

    if (!HDC.HardFile[currentdrive]) {
      fprintf(stderr,"HDC: Couldn't open image for drive %d\n", currentdrive);
    }
  } /* Image opening */

  HDC.DREQ=false;
} /* HDC_Init */

