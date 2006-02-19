/* (c) David Alan Gilbert 1995-1998 - see Readme file for copying info */

/*#define DEBUG_MEMC */
/*
    Used to be based on ArmVirt.c

    Modified by Dave Gilbert (arcem@treblig.org) to emulate an Archimedes

    Added support for 8MB and 16MB RAM configurations - Matthew Howkins
*/

#include <signal.h>
#include <stdio.h>
#include <string.h>
//#include <unistd.h>
#include <math.h>

#include "../armopts.h"
#include "../armdefs.h"
#include "../armrdi.h"

#include "dbugsys.h"

#include "armarc.h"
#include "archio.h"
#include "DispKbd.h"
#include "archio.h"
#include "fdc1772.h"
#include "ReadConfig.h"
#include "Version.h"
#include "extnrom.h"
#include "ArcemConfig.h"


#ifdef MACOSX
#include <unistd.h>
extern char arcemDir[256];
void arcem_exit(char* msg);
#endif

#ifdef __riscos__
#include "kernel.h"
#include "swis.h"

extern int *ROScreenMemory;
extern int ROScreenExtent;

extern int MonitorBpp;

int Vstartold;
int Vinitold;
int Vendold;

#endif

/* Page size flags */
#define MEMC_PAGESIZE_O_4K     0
#define MEMC_PAGESIZE_1_8K     1
#define MEMC_PAGESIZE_2_16K    2
#define MEMC_PAGESIZE_3_32K    3


struct MEMCStruct memc;

#ifdef SOUND_SUPPORT
static SoundData soundTable[256];
static double channelAmount[8][2];
static unsigned int numberOfChannels;
#endif

/*-----------------------------------------------------------------------------*/


#define PRIVILEGED (statestr.NtransSig)

#define OSMODE(state) ((MEMC.ControlReg >> 12) & 1)

static ARMul_State *sigavailablestate=NULL;

#ifdef SOUND_SUPPORT
static void SoundInitTable(void);
#endif

/*------------------------------------------------------------------------------*/
/* OK - this is getting treated as an odds/sods engine - just hook up anything
   you need to do occasionally! */
static void DumpHandler(int sig) {
  FILE *res;

  exit(0);

  fprintf(stderr,"SIGUSR2 at PC=0x%x R[15]=0x%x\n",(unsigned int)statestr.pc,
                                            (unsigned int)statestr.Reg[15]);
#ifndef WIN32
  signal(SIGUSR2,DumpHandler);
#endif

  exit(0);
  res=fopen("memdump","wb");

  if (res==NULL) {
    fprintf(stderr,"Could not open memory dump file\n");
    return;
  };

  fwrite(MEMC.PhysRam,1,/*MEMC.RAMSize*/ 512*1024,res);

  fclose(res);
} /* DumpHandler */

/**
 * GetPhysAddress
 *
 * Uses MEMC.TmpPage which must have been previously been set up by a call to
 * checkabort
 *
 * @param address  Logical RAM address
 * @returns        Physical RAM address
 */
ARMword
GetPhysAddress(unsigned address)
{
  ARMword pagetabval = MEMC.TmpPage;
  unsigned PhysPage;

  if (pagetabval == -1)
    return 0;

  switch (MEMC.PageSizeFlags) {
    case MEMC_PAGESIZE_O_4K:
      /* 4K */
      PhysPage = pagetabval & 127;
      return (address & 0xfff) | (PhysPage << 12);

    case MEMC_PAGESIZE_1_8K:
      /* 8K */
      PhysPage = ((pagetabval >> 1) & 0x3f) | ((pagetabval & 1) << 6);
      return (address & 0x1fff) | (PhysPage << 13);

    case MEMC_PAGESIZE_2_16K:
      /* 16K */
      PhysPage = ((pagetabval >> 2) & 0x1f) | ((pagetabval & 3) << 5);
      return (address & 0x3fff) | (PhysPage << 14);

    case MEMC_PAGESIZE_3_32K:
      /* 32K */
      PhysPage = ((pagetabval >> 3) & 0xf) | ((pagetabval & 1) << 4) |
                 ((pagetabval & 2) << 5) | ((pagetabval & 4) << 3) |
                 (pagetabval & 0x80) | ((pagetabval >> 4) & 0x100);
      return (address & 0x7fff) | (PhysPage << 15);
  } /* Page size switch */

  abort();
} /* GetPhysAddress */


/**
 * FindPageTableEntry_Search
 *
 * Look up an address in the pagetable and return the raw entry
 * Return 1 if we find it
 *
 * @param address
 * @param PageTabVal
 * @returns 1 if we find it
  */
static int
FindPageTableEntry_Search(unsigned address, ARMword *PageTabVal)
{
  int Current;
  unsigned int matchaddr;
  unsigned int mask;

  switch (MEMC.PageSizeFlags) {
    case MEMC_PAGESIZE_O_4K: /* 4K */
      matchaddr = ((address & 0x7ff) << 12) |
                  ((address & 0x1800) >> 1);
      mask = 0x807ffc00;
      break;

    case MEMC_PAGESIZE_1_8K: /* 8K */
      matchaddr = ((address & 0x3ff) << 13) |
                   (address & 0xc00);
      mask = 0x807fec00;
      break;

    case MEMC_PAGESIZE_2_16K: /* 16K */
      matchaddr = ((address & 0x1ff) << 14) |
                  ((address & 0x600) << 1);
      mask = 0x807fcc00;
      break;

    case MEMC_PAGESIZE_3_32K: /* 32K */
      matchaddr = ((address & 0xff) << 15) |
                  ((address & 0x300) << 2);
      mask = 0x807f8c00;
      break;

    default:
      abort();
  }

  for (Current = 0; Current < 512; Current++) {
    if ((MEMC.PageTable[Current] & mask) == matchaddr) {
      MEMC.OldAddress1 = address;
      MEMC.OldPage1 = MEMC.PageTable[Current];
      *PageTabVal   = MEMC.PageTable[Current];
      return 1;
    }
  }

  return 0;
} /* FindPageTableEntry_Search */


/** FindPageTableEntry
 *
 * @param address
 * @param PageTabVal
 * @returns 1 if page entry for address found
 */
int
FindPageTableEntry(unsigned address, ARMword *PageTabVal)
{
  address >>= (12 + MEMC.PageSizeFlags);
  if (address == MEMC.OldAddress1) {
    *PageTabVal = MEMC.OldPage1;
    return 1;
  }

  return FindPageTableEntry_Search(address, PageTabVal);
}


/**
 * CheckAbortR
 *
 * Would a read to the address cause an abort?
 *
 * @param address Address to check
 * @returns 0 if we should abort. non zero if fine
 */
static int CheckAbortR(int address) {
  ARMword PageTabVal;

  /*printf("CheckAbort for address=0x%x\n",address);*/

  /* First check if we are doing ROM remapping and are accessing a remapped location */
  /* If it is then map it into the ROM space */
  if ((MEMC.ROMMapFlag) && (address <0xc00000))
    address = MEMORY_0x3400000_R_ROM_LOW + address;

  switch ((address >> 24) & 3) {
    case 3: /* 0x03000000 - 0x03FFFFFF - 16MB - IO Controllers (8MB) and ROM space 2 x 4MB */
      /* If we are in ROM and trying to read then there is no hassle */
      if (address >= MEMORY_0x3400000_R_ROM_LOW) {
        /* It's ROM read - no hassle */
        return(1);
      }
      /* NOTE: No break, IOC has same permissions as physical RAM */

    case 2: /* 0x02000000 - 0x02FFFFFF - 16MB - Phyisically mapped Ram */
      /* Privileged only */
      return PRIVILEGED;

    case 0: /* 0x00000000 - 0x01FFFFFF - 32MB - Logically mapped Ram */
    case 1:
      /* OK - it's in logically mapped RAM */
      if (!FindPageTableEntry(address,&PageTabVal)) {
        MEMC.TmpPage=-1;
        dbug("ChecKAbortR at 0x%08x due to unmapped page\n",address);
        return(0);
      }

      MEMC.TmpPage=PageTabVal;

      /* The page exists - so if it's supervisor then it's OK */
      if (PRIVILEGED)
        return(1);

      if (MEMC.ControlReg & (1<<12)) {
        /* OS mode */
        /* it can read anything */
        return(1);
      } else {
        /* User mode */
        /* Extract page protection level */
        if (PageTabVal & 512) {
          MEMC.TmpPage=-1;
          dbug("CheckAbortR at 0x%08x due to bad user mode PPL PageTabVal=0x%x\n",address,PageTabVal);
          return(0);
        } else
          return 1;
      } /* User mode */
  }
  return 0;
} /* CheckAbortR */


/**
 * CheckAbortW
 *
 * Would a write to the address cause an abort?
 *
 * @param address Address to check
 * @returns 0 if we should abort, non zero if fine
 */
static int CheckAbortW(int address) {
  ARMword PageTabVal;

  /*printf("CheckAbort for address=0x%x\n",address);*/

  /* All writes to Physically mapped Ram, IO controllers, Video Controller
     DMA Address Generator, MEMC control register, and to the Logical to
     physical address translator all are only allowed in Supervisor mode
     (PRIVELEGED) */
  if (address >= MEMORY_0x2000000_RAM_PHYS) {
    /*fprintf(stderr,"Ntrans=%d\n",statestr.NtransSig); */
    return PRIVILEGED;
  }

  /* OK - it's in logically mapped RAM */
  if (!FindPageTableEntry(address, &PageTabVal)) {
    MEMC.TmpPage = -1;
    dbug("Failed CheckAbortW at address0x%08x due to inability to find page\n", address);
    return(0);
  }

  MEMC.TmpPage = PageTabVal;

  /* The page exists - so if it's superviser then it's OK */
  if (PRIVILEGED)
    return(1);

  /* Extract page protection level */
  PageTabVal >>= 8;
  PageTabVal &= 3;

  if (MEMC.ControlReg & (1<<12)) {
    /* OS mode */

    /* it can only write if the top bit of the PPL is low */
    if (PageTabVal & 2) {
      MEMC.TmpPage = -1;
      return(0);
    } else {
      return(1);
    }

  } else {
    /* User mode */
    if (PageTabVal) {
      MEMC.TmpPage = -1;
      return 0;

    } else {
      return 1;
    }

  } /* User mode */

  /* Hey - it shouldn't get here */
  abort();
  return(0);
} /* CheckAbort */


/**
 * ARMul_MemoryInit
 *
 * Initialise the memory interface, Initialise memory map, load Rom,
 * load extension Roms
 *
 * @param state
 * @param initmemorysize If non zero, then use this for the ammount of physical ram
 *                       in bytes. If 0 default to 4MB
 * @returns
 */
unsigned
ARMul_MemoryInit(ARMul_State *state, unsigned long initmemsize)
{
  PrivateDataType *PrivDPtr;
  FILE *ROMFile;
  unsigned int ROMWordNum, ROMWord;
  int PresPage;
  unsigned int i;
  unsigned extnrom_size = 0;
#if defined(SYSTEM_X) || defined(MACOSX) || defined(SYSTEM_win)
  unsigned extnrom_entry_count;
#endif

  PrivDPtr = calloc(sizeof(PrivateDataType), 1);
  if (PrivDPtr == NULL) {
    fprintf(stderr,"ARMul_MemoryInit: malloc of PrivateDataType failed\n");
    exit(3);
  }

  statestr.MemDataPtr = (unsigned char *)PrivDPtr;

  dbug("Reading config file....\n");
  ReadConfigFile(state);

  if (initmemsize) {
   statestr.MemSize = initmemsize;
  } else {
    statestr.MemSize = initmemsize = 4 * 1024 * 1024; /* seems like a good value! */
  }

  sigavailablestate = state;
#ifndef WIN32
  signal(SIGUSR2,DumpHandler);
#endif

  MEMC.RAMSize = initmemsize;
  /* and now the RAM */
  /* Was malloc - but I want it 0 init */
  MEMC.PhysRam = calloc(MEMC.RAMSize,1);
  MEMC.PhysRamfuncs = malloc(((MEMC.RAMSize) / 4) * sizeof(ARMEmuFunc));
  if ((MEMC.PhysRam == NULL) || (MEMC.PhysRamfuncs == NULL)) {
    fprintf(stderr,"Couldn't allocate RAM space\n");
    exit(3);
  }

  for (i = 0; i < MEMC.RAMSize / 4; i++) {
    MEMC.PhysRamfuncs[i] = ARMul_Emulate_DecodeInstr;
  }

  MEMC.ROMMapFlag    = 1; /* Map ROM to address 0 */
  MEMC.ControlReg    = 0; /* Defaults */
  MEMC.PageSizeFlags = MEMC_PAGESIZE_O_4K;
  MEMC.NextSoundBufferValid = 0; /* Not set till Sstart and SendN have been written */

#ifdef SOUND_SUPPORT
  SoundInitTable();
#endif

  /* Top bit set means it isn't valid */
  for (PresPage = 0; PresPage < 512; PresPage++)
    MEMC.PageTable[PresPage] = 0xffffffff;

  dbug(" Loading ROM....\n ");


#ifdef __riscos__
  if (ROMFile = fopen("<ArcEm$Dir>.^." hArcemConfig.sRomImageName, "rb"), ROMFile == NULL) {
    exit(2);
  }
#else
#ifdef MACOSX
  {
    chdir(arcemDir);
  }
  if (ROMFile = fopen(hArcemConfig.sRomImageName, "rb"), ROMFile == NULL) {
    arcem_exit("Couldn't open ROM file");
  }
#else
  if (ROMFile = fopen(hArcemConfig.sRomImageName, "rb"), ROMFile == NULL) {
    fprintf(stderr, "Couldn't open ROM file\n");
    exit(EXIT_FAILURE);
  }
#endif
#endif

  /* Find the rom file size */
  fseek(ROMFile, 0l, SEEK_END);

  MEMC.ROMHighSize = (ARMword) ftell(ROMFile);

  fseek(ROMFile, 0l, SEEK_SET);

#if defined(EXTNROM_SUPPORT)
  /* Add the space required by an Extension Rom */
  extnrom_size = extnrom_calculate_size(&extnrom_entry_count);
  fprintf(stderr, "extnrom_size = %u, extnrom_entry_count= %u\n",
          extnrom_size, extnrom_entry_count);
#endif /* EXTNROM_SUPPORT */
  dbug("Total ROM size required = %u KB\n",
       (MEMC.ROMHighSize + extnrom_size) / 1024);

  /* Allocate the space for the system ROM in ROMHigh */
  /* 128 is to try and ensure gap with next malloc to stop cache thrashing */
  MEMC.ROMHigh = calloc(MEMC.ROMHighSize + 128, 1);
  MEMC.ROMHighFuncs = malloc((MEMC.ROMHighSize / 4) * sizeof(ARMEmuFunc));

  if ((MEMC.ROMHigh == NULL) || (MEMC.ROMHighFuncs == NULL)) {
    fprintf(stderr,"Couldn't allocate space for ROM High\n");
    exit(3);
  }

  for (ROMWordNum = 0; ROMWordNum < MEMC.ROMHighSize / 4; ROMWordNum++) {
#ifdef DEBUG
    if (!(ROMWordNum & 0x1ff)) {
      fprintf(stderr, ".");
      fflush(stderr);
    }
#endif

    ROMWord  =  fgetc(ROMFile);
    ROMWord |= (fgetc(ROMFile) << 8);
    ROMWord |= (fgetc(ROMFile) << 16);
    ROMWord |= (fgetc(ROMFile) << 24);
    MEMC.ROMHigh[ROMWordNum] = ROMWord;
    MEMC.ROMHighFuncs[ROMWordNum] = ARMul_Emulate_DecodeInstr;
  }

  /* Close System ROM Image File */
  fclose(ROMFile);

  /* Create Space for extension ROM in ROMLow */
  if (extnrom_size) {
    MEMC.ROMLowSize = extnrom_size;

    /* calloc() now used to ensure that Extension ROM space is zero'ed */
    MEMC.ROMLow = calloc(MEMC.ROMLowSize, 1);
    MEMC.ROMLowFuncs = malloc((MEMC.ROMLowSize / 4) * sizeof(ARMEmuFunc));

    if ((MEMC.ROMLow == NULL) || (MEMC.ROMLowFuncs == NULL)) {
      fprintf(stderr,"Couldn't allocate space for ROM Low\n");
      exit(3);
    }
#if defined(EXTNROM_SUPPORT)
    /* Load extension ROM */
    dbug("Loading Extension ROM...\n");
    extnrom_load(extnrom_size, extnrom_entry_count,
                 (unsigned char *) MEMC.ROMLow);
#endif /* EXTNROM_SUPPORT */

    for (ROMWordNum = 0; ROMWordNum < MEMC.ROMLowSize / 4; ROMWordNum++) {
      MEMC.ROMLowFuncs[ROMWordNum] = ARMul_Emulate_DecodeInstr;
    }
  }

  dbug(" ..Done\n ");

  IO_Init(state);
  DisplayKbd_Init(state);

  PRIVD->irqflags = 0;
  PRIVD->fiqflags = 0;

  for (i = 0; i < 512 * 1024 / UPDATEBLOCKSIZE; i++) {
    MEMC.UpdateFlags[i] = 1;
  }

  MEMC.OldAddress1 = -1;
  MEMC.OldPage1    = -1;

#ifdef DEBUG
  ARMul_ConsolePrint(state, " Archimedes memory ");
  ARMul_ConsolePrint(state, Version);
  ARMul_ConsolePrint(state, "\n");
#endif

  return(TRUE);
}

/**
 * ARMul_MemoryExit
 *
 * Remove the memory interface
 *
 * @param state
 */
void ARMul_MemoryExit(ARMul_State *state)
{
  free(MEMC.PhysRam);
  free(MEMC.ROMHigh);
  free(MEMC.ROMHighFuncs);
  if (MEMC.ROMLow) {
    free(MEMC.ROMLow);
  }
  if (MEMC.ROMLowFuncs) {
    free(MEMC.ROMLowFuncs);
  }
  free(PRIVD);
}

/**
 * ARMul_LoadWordS
 *
 * Load Word, Sequential Cycle
 *
 * @param state
 * @param address
 * @returns
 */
ARMword ARMul_LoadWordS(ARMul_State *state, ARMword address)
{
  statestr.NumCycles++;
  address &= 0x3ffffff;

  if (CheckAbortR(address)) {
    ARMul_CLEARABORT;
  } else {
    dbug("ARMul_LoadWordS abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",
         address, statestr.pc, statestr.Reg[15], PRIVILEGED, OSMODE(state), statestr.Mode);
    ARMul_DATAABORT(address);
    return(ARMul_ABORTWORD);
  }

  return GetWord(address);
}

/**
 * ARMul_LoadWordN
 *
 * Load Word, Non Sequential Cycle
 *
 * @param state
 * @param address
 * @returns
 */
ARMword ARMul_LoadWordN(ARMul_State *state, ARMword address)
{
  statestr.NumCycles++;
  address &= 0x3ffffff;

  if (CheckAbortR(address)) {
    ARMul_CLEARABORT;
  } else {
    dbug("ARMul_LoadWordN abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",
         address, statestr.pc, statestr.Reg[15], PRIVILEGED, OSMODE(state), statestr.Mode);
    ARMul_DATAABORT(address);
    return(ARMul_ABORTWORD);
  }

  return GetWord(address);
}

/**
 * ARMul_LoadByte
 *
 * Load Byte, (Non Sequential Cycle)
 *
 * @param state
 * @param address
 * @returns
 */
ARMword ARMul_LoadByte(ARMul_State *state, ARMword address) {
  unsigned int temp;
  int bytenum = address & 3;

  address &= 0x3ffffff;

  statestr.NumCycles++;

  if (CheckAbortR(address)) {
    ARMul_CLEARABORT;
  } else {
    dbug("ARMul_LoadByte abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",
         address, statestr.pc, statestr.Reg[15], PRIVILEGED, OSMODE(state), statestr.Mode);
    ARMul_DATAABORT(address);
    return(ARMul_ABORTWORD);
  }

  temp = GetWord(address);

  /* WARNING! Little endian only */
  temp>>=(bytenum*8);

  return(temp & 0xff);
}

/**
 * ARMul_StoreWordS
 *
 * Store Word, Sequential Cycle
 *
 * @param state
 * @param address
 * @param data
 */
void ARMul_StoreWordS(ARMul_State *state, ARMword address, ARMword data)
{
  statestr.NumCycles++;
  address &= 0x3ffffff;

  if (CheckAbortW(address)) {
    ARMul_CLEARABORT;
  } else {
    dbug("ARMul_StoreWordS abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",
         address, statestr.pc, statestr.Reg[15], PRIVILEGED, OSMODE(state), statestr.Mode);
    ARMul_DATAABORT(address);
    return;
  }

  PutWord(state, address, data);
}

/**
 * ARMul_StoreWordN
 *
 * Store Word, Non Sequential Cycle
 *
 * @param state
 * @param address
 * @param data
 */
void ARMul_StoreWordN(ARMul_State *state, ARMword address, ARMword data)
{
  statestr.NumCycles++;
  address &= 0x3ffffff;

  if (CheckAbortW(address)) {
    ARMul_CLEARABORT;
  } else {
    dbug("ARMul_StoreWordN abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",
         address, statestr.pc, statestr.Reg[15], PRIVILEGED, OSMODE(state), statestr.Mode);
    ARMul_DATAABORT(address);
    return;
  }

  PutWord(state, address, data);
}

/**
 * ARMul_StoreByte
 *
 * Store Byte, (Non Sequential Cycle)
 *
 * @param state
 * @param address
 * @param data
 */
void ARMul_StoreByte(ARMul_State *state, ARMword address, ARMword data)
{
  statestr.NumCycles++;
  address &= 0x3ffffff;

  if (CheckAbortW(address)) {
    ARMul_CLEARABORT;
  } else {
    ARMul_DATAABORT(address);
    return;
  }

  PutVal(state, address, data, 1, 1, ARMul_Emulate_DecodeInstr);
}


/**
 * ARMul_SwapWord
 *
 * Swap Word, (Two Non Sequential Cycles)
 *
 * @param state
 * @param address
 * @param data
 * @returns
 */
ARMword ARMul_SwapWord(ARMul_State *state, ARMword address, ARMword data)
{
  ARMword temp;

  statestr.NumCycles++;
  address &= 0x3ffffff;

  if (CheckAbortW(address)) {
    ARMul_CLEARABORT;
  } else {
    ARMul_DATAABORT(address);
    return(ARMul_ABORTWORD);
  }

  temp = GetWord(address);
  statestr.NumCycles++;
  PutWord(state, address, data);
  return(temp);
}

/**
 * ARMul_SwapByte
 *
 * Swap Byte, (Two Non Sequential Cycles)
 *
 * @param state
 * @param address
 * @param data
 * @returns
 */
ARMword ARMul_SwapByte(ARMul_State *state, ARMword address, ARMword data)
{
  ARMword temp;

  address &= 0x3ffffff;
  if (CheckAbortW(address)) {
    ARMul_CLEARABORT;
  } else {
    ARMul_DATAABORT(address);
    return(ARMul_ABORTWORD);
  }

  temp = ARMul_LoadByte(state, address);
  ARMul_StoreByte(state, address, data);
  return(temp);
}

/**
 * GetWord
 *
 * Return the contents of an address in the archimedes memory map.
 * The caller MUST perform a full abort check first - if only to ensure that
 * a page in logical RAM does not abort.
 *
 * @param address Address to get value of
 * @returns Content of address
 */
ARMword GetWord(ARMword address) {

  /* First check if we are doing ROM remapping and are accessing a remapped */
  /* location.  If it is then map it into the High ROM space                */
  if ((MEMC.ROMMapFlag) && (address < 0x800000)) {
    /* A simple ROM wrap around */
    return MEMC.ROMHigh[(address % MEMC.ROMHighSize) / 4];
  }

  switch ((address >> 24) & 3) {
    case 3: /* 0x03000000 - 0x03FFFFFF - 16MB - IO Controllers (4MB) and ROM space 4 + 8MB */
      if (address >= MEMORY_0x3400000_R_ROM_LOW) {

        if ((MEMC.ROMMapFlag == 2)) {
          MEMC.OldAddress1 = -1;
          MEMC.OldPage1 = -1;
          MEMC.ROMMapFlag = 0;
        }

        /* Both ROM areas wrap around */
        if (address >= MEMORY_0x3800000_R_ROM_HIGH) {
          address -= MEMORY_0x3800000_R_ROM_HIGH;
          return MEMC.ROMHigh[(address % MEMC.ROMHighSize) / 4];
        } else {
          if (MEMC.ROMLow) {
            address -= MEMORY_0x3400000_R_ROM_LOW;
            return MEMC.ROMLow[(address % MEMC.ROMLowSize) / 4];
          } else {
            return 0;
          }
        }
      }

      if ((MEMC.ROMMapFlag == 2)) {
        MEMC.ROMMapFlag = 0;
        MEMC.OldAddress1 = -1;
        MEMC.OldPage1 = -1;
      }

      return GetWord_IO(emu_state,address);

    case 2: /* 0x02000000 - 0x02FFFFFF - 16MB - Phyisically mapped Ram */
      if (MEMC.ROMMapFlag == 2) {
        MEMC.ROMMapFlag = 0;
        MEMC.OldAddress1 =- 1;
        MEMC.OldPage1 =- 1;
      }
      address -= MEMORY_0x2000000_RAM_PHYS;

      /* I used to think memory wrapped after the real RAM - but it doesn't */
      /* appear to */
      if  (address >= MEMC.RAMSize) {
        dbug_memc("GetWord returning dead0bad - after PhysRam\n");
        return(0xdead0bad);
      }

      return MEMC.PhysRam[address / 4];

    case 0: /* 0x00000000 - 0x01FFFFFF - 32MB - Logically mapped Ram */
    case 1:
      /* Logically mapped RAM */
      address = GetPhysAddress(address);

      /* I used to think memory wrapped after the real RAM - but it doesn't */
      /* appear to */
      if (address >= MEMC.RAMSize) {
        dbug_memc("GetWord returning dead0bad - after PhysRam (from log ram)\n");
        return(0xdead0bad);
      }

      return (MEMC.PhysRam[address / 4]);
  }
  return 0;
} /* GetWord */

/**
 * PutVal
 *
 * Set a value of an address in the archimedes memory map
 * The caller MUST check for aborts before calling us!
 *
 * @param state        Emulator state
 * @param address      Address to write to
 * @param data         Data to write
 * @param byteNotword  Non-zero if this is a byte store, not a word store
 * @param Statechange
 * @param newfunc
 */
void
PutVal(ARMul_State *state, ARMword address, ARMword data, int byteNotword,
       int Statechange, ARMEmuFunc newfunc)
{
  if (address >= MEMORY_0x3800000_W_LOG2PHYS) {
    /* Logical-to-physical address translation */
    unsigned tmp;

    if (!Statechange)
      return;

    MEMC.OldAddress1 = -1;
    MEMC.OldPage1 = -1;

    if (MEMC.ROMMapFlag == 2)
      MEMC.ROMMapFlag = 0;

    tmp = address - MEMORY_0x3800000_W_LOG2PHYS;
#ifdef DEBUG_MEMC
    {
      unsigned logbaseaddr;
      unsigned Entry;

      logbaseaddr =  (tmp >> 15) & 0xff;
      logbaseaddr |= (tmp >> 2) & 0x300;
      logbaseaddr <<= (12 + MEMC.PageSizeFlags); /* Actually all that masking is only OK for 32K */

      Entry =  (tmp >> 3) & 0xf;
      Entry |= (tmp << 4) & 0x10;
      Entry |= (tmp << 3) & 0x20;
      Entry |= (tmp << 5) & 0x40;
      Entry |= tmp & 0x80;
      Entry |= (tmp >> 4) & 0x100;
      fprintf(stderr,"PutVal: Pagetable write, address=0x%x entry=0x%x val=0x%x (logbaseaddress=0x%08x ppn=%d - for 32K size)\n",
              address, ((address >> 4) & 0x100) | (address & 0xff),
              tmp & 0x0fffffff, logbaseaddr, Entry);
    }
#endif

    MEMC.PageTable[((address >> 4) & 0x100) | (address & 0xff)] = tmp & 0x0fffffff;

    return;
  }

  if (address >= MEMORY_0x3600000_W_DMA_GEN) {
    /* DMA address generation - MEMC Control reg */
    ARMword RegNum,RegVal;

    if (!Statechange)
      return;

    if (MEMC.ROMMapFlag == 2)
      MEMC.ROMMapFlag = 0;

    RegNum = (address >> 17) & 7;
    RegVal = (address >> 2) & 0x7fff;

    dbug_memc("Write to MEMC register: Reg=%d Val=0x%x\n",RegNum,RegVal);

    switch (RegNum) {
      case 0: /* Vinit */
        VideoRelUpdateAndForce(DISPLAYCONTROL.MustRedraw, MEMC.Vinit, RegVal);
        break;

      case 1: /* Vstart */
        VideoRelUpdateAndForce(DISPLAYCONTROL.MustRedraw, MEMC.Vstart, RegVal);
        break;

      case 2: /* Vend */
        VideoRelUpdateAndForce(DISPLAYCONTROL.MustRedraw, MEMC.Vend, RegVal);
        break;

      case 3: /* Cinit */
        MEMC.Cinit = RegVal;
        break;

      case 4: /* Sstart */
        RegVal *= 16;
        MEMC.Sstart = RegVal;
        /* The data sheet does not define what happens if you write start before end. */
        MEMC.NextSoundBufferValid = 1;
        ioc.IRQStatus &= ~0x200; /* Take sound interrupt off */
        IO_UpdateNirq();
        dbug_memc("Write to MEMC Sstart register\n");
        break;

      case 5: /* SendN */
        RegVal *= 16;
        MEMC.SendN = RegVal;
        dbug_memc("Write to MEMC SendN register\n");
        break;

      case 6: /* Sptr */
        dbug_memc("Write to MEMC Sptr register\n");
        /* Note this never actually sets Sptr directly, instead
         * it causes a first buffer to be swapped in. */
        {
          ARMword swap;
          MEMC.Sptr = MEMC.Sstart;
          swap = MEMC.SendC;
          MEMC.SendC = MEMC.SendN;
          MEMC.SendN = swap;
          MEMC.SstartC = MEMC.Sptr;
          MEMC.NextSoundBufferValid = 0;
          ioc.IRQStatus |= 0x200; /* Take sound interrupt on */
          IO_UpdateNirq();
        }
        break;

      case 7: /* MEMC Control register */
        dbug_memc("MEMC Control register set to 0x%x by PC=0x%x R[15]=0x%x\n",
                  address, (unsigned int)statestr.pc, (unsigned int)statestr.Reg[15]);
        MEMC.ControlReg = address;
        MEMC.PageSizeFlags = (MEMC.ControlReg & 12) >> 2;
        MEMC.OldAddress1 = -1;
        MEMC.OldPage1 = -1;
        break;
    }
    return;
  } /* DMA address gen/MEMC control */

  /* VIDC Space */
  if (address >= MEMORY_0x3400000_W_VIDEOCON) {
    if (!Statechange)
      return;

    if (MEMC.ROMMapFlag == 2)
      MEMC.ROMMapFlag = 0;
    /* VIDC */
    VIDC_PutVal(state, address, data, byteNotword);
    return;
  }

  if (address >= MEMORY_0x3000000_CON_IO) {
    if (!Statechange)
      return;

    if (MEMC.ROMMapFlag == 2)
      MEMC.ROMMapFlag = 0;
    /* IOC */
    PutValIO(state, address, data, byteNotword);
    return;
  }

  if (address < MEMORY_0x2000000_RAM_PHYS) {
    /* Logically mapped RAM - shift the address so that the physical ram code will do it */
    address = GetPhysAddress(address);
  } else {
    /* Physical ram */
    if (MEMC.ROMMapFlag == 2) {
      MEMC.ROMMapFlag = 0;
    }
    address -= MEMORY_0x2000000_RAM_PHYS;
  }

  if (address >= MEMC.RAMSize) {
    return;
  }

  /* Do updating for VIDC emulation */
  /* See if it's in DMAble ram */
  if (address<512*1024) {
    MEMC.UpdateFlags[address/UPDATEBLOCKSIZE]++;
  }

  /* Handle byte stores */
  if (byteNotword) {
    /* Essentially we read the word in RAM,
       clear the byte we want to write to,
       and fill the byte with the value to be written */
    static const unsigned masktab[] = {
      0xffffff00, 0xffff00ff, 0xff00ffff, 0x00ffffff
    };
    ARMword mask;
    ARMword tmp;
    data &= 0xff;

    mask = masktab[address & 3];
    tmp = MEMC.PhysRam[address / 4] & mask;
    data = tmp | (data << ((address & 3) * 8));
  }

#ifdef DIRECT_DISPLAY
  if (MEMC.Vstart!=Vstartold || MEMC.Vend!=Vendold)
  {
    int size=_swi(OS_ReadDynamicArea, _IN(0)|_RETURN(1), 2);
    int size_wanted=(MEMC.Vend+1-MEMC.Vstart)*16;

    printf("Size wanted: %d, Old size: %d\n",size_wanted,size);

    switch (MonitorBpp)
    {
      case 0:
        size_wanted/=8;
        break;
      case 1:
        size_wanted/=4;
        break;
      case 2:
        size_wanted/=2;
        break;
    }

    _swix(OS_ChangeDynamicArea, _INR(0,1), 2, size_wanted - size);

    size = _swi(OS_ReadDynamicArea, _IN(0) | _RETURN(1), 2);

    printf("Size wanted: %d, New size: %d\n", size_wanted, size);

    {
      int block[3];

      block[0]=148;
      block[1]=150;
      block[2]=-1;

      _swi(OS_ReadVduVariables, _INR(0,1), &block, &block);

      ROScreenMemory = (int *)block[0];
      ROScreenExtent = block[1];
    }

    Vstartold=MEMC.Vstart;
    Vendold=MEMC.Vend;
  }

  if (MEMC.Vinit!=Vinitold)
  {
    char block[5];
    unsigned int pointer=(MEMC.Vinit-MEMC.Vstart)*16;

    switch (MonitorBpp)
    {
      case 0:
        pointer/=8;
        break;
      case 1:
        pointer/=4;
        break;
      case 2:
        pointer/=2;
        break;
    }

    block[0]=(1<<1)+(1<<0);
    block[1]=(pointer & 0x000000ff);
    block[2]=(pointer & 0x0000ff00) >> 8;
    block[3]=(pointer & 0x00ff0000) >> 16;
    block[4]=(pointer & 0xff000000) >> 24;

    _swi(OS_Word, _INR(0,1), 22, &block);

    Vinitold=MEMC.Vinit;
  }

  if (address < ROScreenExtent) {
//    extern void ScreenWrite(int *address, ARMword data);

//    ScreenWrite(ROScreenMemory + address / 4, data);
    ROScreenMemory[address/4] = data;
  }
#endif

  MEMC.PhysRam[address / 4] = data;
  MEMC.PhysRamfuncs[address / 4] = newfunc;

  return;
} /* PutVal */


#ifdef SOUND_SUPPORT
/**
 * SoundDMAFetch
 *
 * Gets and converts one 16 byte block from the current buffer,
 * then does updating like switching to the next buffer.
 *
 * Note that this does not fill the buffer if there is no new buffer,
 * so you are expected to keep the contents yourself if you want it to wrap.
 *
 * The amount of buffer you must provide is 64 bytes
 *
 * @param buffer       Buffer to fill with sound data
 * @param blocks       Number of 4 words blocks to fetch, that is blocks * 16 bytes
 * @returns 0 if the fetch was done, 1 if sound dma is disabled and so no fetch was done.
 */
int
SoundDMAFetch(SoundData *buffer)
{
  int i;

  if ((MEMC.ControlReg & (1 << 11)) == 0) {
    return 1;
  }

  for (i = 0; i < 16; i += numberOfChannels) {
    int j;
    double leftTotal = 0;
    double rightTotal = 0;

    for (j = 0; j < numberOfChannels; j++) {
      unsigned char val = ((unsigned char *) (MEMC.PhysRam + (MEMC.Sptr / sizeof(ARMword))))[i + j];

      leftTotal  += (signed short int) soundTable[val] * channelAmount[j][0];
      rightTotal += (signed short int) soundTable[val] * channelAmount[j][1];
    }

    buffer[2 * i]       = (SoundData) leftTotal;
    buffer[(2 * i) + 1] = (SoundData) rightTotal;
  }

  MEMC.Sptr += 16;

  /* Reached end of buffer? */
  if (MEMC.Sptr + 16 >= MEMC.SendC) {

    /* Have the next buffer addresses been written? */
    if (MEMC.NextSoundBufferValid == 1) {
      /* Yes, so change to the next buffer */
      ARMword swap;

      MEMC.Sptr = MEMC.Sstart;
      MEMC.SstartC = MEMC.Sstart;

      swap = MEMC.SendC;
      MEMC.SendC = MEMC.SendN;
      MEMC.SendN = swap;

      ioc.IRQStatus |= 0x200; /* Take sound interrupt on */
      IO_UpdateNirq();

      MEMC.NextSoundBufferValid = 0;
      return 1;
    } else {
      /* Otherwise wrap to the beginning of the buffer */
      MEMC.Sptr = MEMC.SstartC;
    }
  }

  return 0;
}

static void
SoundInitTable(void)
{
  unsigned i;

  for (i = 0; i < 256; i++) {
    /*
     * (not VIDC1, whoops...)
     * VIDC2:
     * 0 Sign
     * 4,3,2,1 Point on chord
     * 7,6,5 Chord select
     */

    unsigned int chordSelect = (i & 0xE0) >> 5;
    unsigned int pointSelect = (i & 0x1E) >> 1;
    unsigned int sign = (i & 1);

    unsigned int stepSize;

    SoundData scale = (SoundData) 0xFFFF / (247 * 2);
    SoundData chordBase;
    SoundData sample;

    switch (chordSelect) {
      case 0: chordBase = 0;
              stepSize = scale / 16;
              break;
      case 1: chordBase = scale;
              stepSize = (2 * scale) / 16;
              break;
      case 2: chordBase = 3*scale;
              stepSize = (4 * scale) / 16;
              break;
      case 3: chordBase = 7*scale;
              stepSize = (8 * scale) / 16;
              break;
      case 4: chordBase = 15*scale;
              stepSize = (16 * scale) / 16;
              break;
      case 5: chordBase = 31*scale;
              stepSize = (32 * scale) / 16;
              break;
      case 6: chordBase = 63*scale;
              stepSize = (64 * scale) / 16;
              break;
      case 7: chordBase = 127*scale;
              stepSize = (120 * scale) / 16;
              break;
      /* End of chord 7 is 247 * scale. */

      default: chordBase = 0;
               stepSize = 0;
               break;
    }

    sample = chordBase + stepSize * pointSelect;

    if (sign == 1) { /* negative */
      soundTable[i] = ~(sample - 1);
    } else {
      soundTable[i] = sample;
    }
    if (i == 128) {
      soundTable[i] = 0xFFFF;
    }
  }
}

/**
 * SoundUpdateStereoImage
 *
 * Called whenever VIDC stereo image registers are written so that the
 * channelAmount array can be recalculated and the new number of channels can
 * be figured out.
 * Additionally, the sample rate is updated as the SFR has a different
 * meaning depending on the number of channels in use.
 */
void
SoundUpdateStereoImage(void)
{
  int i = 0;

  /* Note that the order of this if block is important for it to
     pick the right number of channels. */

  /* 1 channel mode? */
  if (VIDC.StereoImageReg[0] == VIDC.StereoImageReg[1] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[2] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[3] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[4] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[5] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[6] &&
      VIDC.StereoImageReg[0] == VIDC.StereoImageReg[7])
  {
    numberOfChannels = 1;
  }
  /* 2 channel mode? */
  else if (VIDC.StereoImageReg[0] == VIDC.StereoImageReg[2] &&
           VIDC.StereoImageReg[0] == VIDC.StereoImageReg[4] &&
           VIDC.StereoImageReg[0] == VIDC.StereoImageReg[6] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[3] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[5] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[7])
  {
    numberOfChannels = 2;
  }
  /* 4 channel mode? */
  else if (VIDC.StereoImageReg[0] == VIDC.StereoImageReg[4] &&
           VIDC.StereoImageReg[1] == VIDC.StereoImageReg[5] &&
           VIDC.StereoImageReg[2] == VIDC.StereoImageReg[6] &&
           VIDC.StereoImageReg[3] == VIDC.StereoImageReg[7])
  {
    numberOfChannels = 4;
  }
  /* Otherwise it is 8 channel mode. */
  else {
    numberOfChannels = 8;
  }

  for (i = 0; i < 8; i++) {
    switch (VIDC.StereoImageReg[i]) {
      /* Centre */
      case 4: channelAmount[i][0] = 0.5;
              channelAmount[i][1] = 0.5;
              break;

      /* Left 100% */
      case 1: channelAmount[i][0] = 1.0;
              channelAmount[i][1] = 0.0;
              break;
      /* Left 83% */
      case 2: channelAmount[i][0] = 0.83;
              channelAmount[i][1] = 0.17;
              break;
      /* Left 67% */
      case 3: channelAmount[i][0] = 0.67;
              channelAmount[i][1] = 0.33;
              break;

      /* Right 100% */
      case 5: channelAmount[i][1] = 1.0;
              channelAmount[i][0] = 0.0;
              break;
      /* Right 83% */
      case 6: channelAmount[i][1] = 0.83;
              channelAmount[i][0] = 0.17;
              break;
      /* Right 67% */
      case 7: channelAmount[i][1] = 0.67;
              channelAmount[i][0] = 0.33;
              break;
    }

    /* We have to share each stereo side between the number of channels. */
    channelAmount[i][0] /= numberOfChannels;
    channelAmount[i][1] /= numberOfChannels;
  }

  SoundUpdateSampleRate();
}

void
SoundUpdateSampleRate(void)
{
  /* SFR = (N - 2) where N is sample period in microseconds. */
  if (numberOfChannels != 8) {
    sound_setSampleRate(VIDC.SoundFreq + 2);
  } else {
    sound_setSampleRate((VIDC.SoundFreq + 2) * 8);
  }
}
#endif
