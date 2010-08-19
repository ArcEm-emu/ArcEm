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
#endif

#ifdef DIRECT_DISPLAY

extern int *DirectScreenMemory;
extern int DirectScreenExtent;

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
static unsigned int numberOfChannels = 0;
#endif

/*-----------------------------------------------------------------------------*/


#ifdef SOUND_SUPPORT
static void SoundInitTable(void);
#endif

static void ARMul_RebuildFastMap(void);

/*------------------------------------------------------------------------------*/
/* OK - this is getting treated as an odds/sods engine - just hook up anything
   you need to do occasionally! */
static void DumpHandler(ARMul_State *state,int sig) {
  FILE *res;

  exit(0);

  fprintf(stderr,"SIGUSR2 at PC=0x%x R[15]=0x%x\n",(unsigned int)state->pc,
                                            (unsigned int)state->Reg[15]);
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
#if defined(EXTNROM_SUPPORT)
  unsigned extnrom_entry_count;
#endif

  PrivDPtr = calloc(sizeof(PrivateDataType), 1);
  if (PrivDPtr == NULL) {
    fprintf(stderr,"ARMul_MemoryInit: malloc of PrivateDataType failed\n");
    exit(3);
  }

  state->MemDataPtr = (unsigned char *)PrivDPtr;

  dbug("Reading config file....\n");
  ReadConfigFile(state);

  if (!initmemsize)
    initmemsize = 4 * 1024 * 1024; /* seems like a good value! */

#ifndef WIN32
  signal(SIGUSR2,DumpHandler);
#endif

  /* Top bit set means it isn't valid */
  for (PresPage = 0; PresPage < 512; PresPage++)
    MEMC.PageTable[PresPage] = 0xffffffff;

  MEMC.ROMMapFlag    = 1; /* Map ROM to address 0 */
  MEMC.ControlReg    = 0; /* Defaults */
  MEMC.PageSizeFlags = MEMC_PAGESIZE_O_4K;
  MEMC.NextSoundBufferValid = 0; /* Not set till Sstart and SendN have been written */

  MEMC.RAMSize = initmemsize;
  MEMC.RAMMask = initmemsize-1;

#ifdef MACOSX
  {
    chdir(arcemDir);
  }
  if (ROMFile = fopen(hArcemConfig.sRomImageName, "rb"), ROMFile == NULL) {
    arcem_exit("Couldn't open ROM file");
  }
#else
#ifdef SYSTEM_gp2x
  if (ROMFile = fopen("/mnt/sd/arcem/rom", "rb"), ROMFile == NULL) {
#else
  if (ROMFile = fopen(hArcemConfig.sRomImageName, "rb"), ROMFile == NULL) {
#endif
    fprintf(stderr, "Couldn't open ROM file\n");
    exit(EXIT_FAILURE);
  }
#endif

  /* Find the rom file size */
  fseek(ROMFile, 0l, SEEK_END);

  MEMC.ROMHighSize = (((ARMword) ftell(ROMFile))+4093)&~4093;
  MEMC.ROMHighMask = MEMC.ROMHighSize-1;

  if(MEMC.ROMHighSize & MEMC.ROMHighMask) {
    fprintf(stderr,"ROM High isn't power of 2 in size\n");
    exit(3);
  }

  fseek(ROMFile, 0l, SEEK_SET);

#if defined(EXTNROM_SUPPORT)
  /* Add the space required by an Extension Rom */
  extnrom_size = (extnrom_calculate_size(&extnrom_entry_count)+4093)&~4093;
  fprintf(stderr, "extnrom_size = %u, extnrom_entry_count= %u\n",
          extnrom_size, extnrom_entry_count);
#endif /* EXTNROM_SUPPORT */
  dbug("Total ROM size required = %u KB\n",
       (MEMC.ROMHighSize + extnrom_size) / 1024);

  /* Now allocate ROMs & RAM in one chunk */
  MEMC.ROMRAMChunk = calloc(MEMC.RAMSize+MEMC.ROMHighSize+extnrom_size+256,1);
  MEMC.EmuFuncChunk = calloc(MEMC.RAMSize+MEMC.ROMHighSize+extnrom_size+256,1);
  if((MEMC.ROMRAMChunk == NULL) || (MEMC.EmuFuncChunk == NULL)) {
    fprintf(stderr,"Couldn't allocate ROMRAMChunk/EmuFuncChunk\n");
    exit(3);
  }
  MEMC.FastMapInstrFuncOfs = ((FastMapUInt)MEMC.EmuFuncChunk)-((FastMapUInt)MEMC.ROMRAMChunk);
  /* Get everything 256 byte aligned for FastMap to work */
  MEMC.PhysRam = (ARMword*) ((((FastMapUInt)MEMC.ROMRAMChunk)+255)&~255); /* RAM must come first for FastMap_LogRamFunc to work! */
  MEMC.ROMHigh = MEMC.PhysRam + (MEMC.RAMSize>>2);

  dbug(" Loading ROM....\n ");

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
  }

  /* Close System ROM Image File */
  fclose(ROMFile);

  /* Create Space for extension ROM in ROMLow */
  if (extnrom_size) {
    MEMC.ROMLowSize = extnrom_size;
    MEMC.ROMLowMask = MEMC.ROMLowSize-1;
  
    if(MEMC.ROMLowSize & MEMC.ROMLowMask) {
      fprintf(stderr,"ROM Low isn't power of 2 in size\n");
      exit(3);
    }

    /* calloc() now used to ensure that Extension ROM space is zero'ed */
    MEMC.ROMLow = MEMC.ROMHigh + (MEMC.ROMHighSize>>2);

#if defined(EXTNROM_SUPPORT)
    /* Load extension ROM */
    dbug("Loading Extension ROM...\n");
    extnrom_load(extnrom_size, extnrom_entry_count,
                 (unsigned char *) MEMC.ROMLow);
#endif /* EXTNROM_SUPPORT */
  }

#ifdef SOUND_SUPPORT
  SoundInitTable();
#endif


  dbug(" ..Done\n ");

  IO_Init(state);
  DisplayKbd_Init(state);

  for (i = 0; i < 512 * 1024 / UPDATEBLOCKSIZE; i++) {
    MEMC.UpdateFlags[i] = 1;
  }

#ifdef DEBUG
  ARMul_ConsolePrint(state, " Archimedes memory ");
  ARMul_ConsolePrint(state, Version);
  ARMul_ConsolePrint(state, "\n");
#endif

  ARMul_RebuildFastMap();
  FastMap_RebuildMapMode(state);

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
  free(MEMC.ROMRAMChunk);
  free(MEMC.EmuFuncChunk);
  free(PRIVD);
}

static void FastMap_SetEntries(ARMword addr,ARMword *data,FastMapAccessFunc func,FastMapUInt flags,ARMword size)
{
  FastMapEntry *entry = FastMap_GetEntryNoWrap(addr);
//  fprintf(stderr,"FastMap_SetEntries(%08x,%08x,%08x,%08x,%08x)\n",addr,data,func,flags,size);
  addr = ((FastMapUInt)data)-addr; /* Offset so we can just add the phy addr to get a pointer back */
  flags |= addr>>8;
//  fprintf(stderr,"->entry %08x\n->FlagsAndData %08x\n",entry,flags);
  while(size) {
    entry->FlagsAndData = flags;
    entry->AccessFunc = func;
    entry++;
    size -= 4096;
  }
}

static void FastMap_SetEntries_Repeat(ARMword addr,ARMword *data,FastMapAccessFunc func,FastMapUInt flags,ARMword size,ARMword totsize)
{
  while(totsize > size) {
    FastMap_SetEntries(addr,data,func,flags,size);
    addr += size;
    totsize -= size;
  }
  /* Should always be something left */
  FastMap_SetEntries(addr,data,func,flags,totsize);
}

static void ARMul_RebuildFastMapPTIdx(ARMword idx);

static void ARMul_PurgeFastMapPTIdx(ARMword idx)
{
  if(MEMC.ROMMapFlag)
    return; /* Still in ROM mode, abort */
    
  int pt = MEMC.PageTable[idx];
  if(pt>0)
  {
    ARMword logadr,phys,mask;
    switch(MEMC.PageSizeFlags) {
      case MEMC_PAGESIZE_O_4K:
        phys = pt & 127;
        logadr = (pt & 0x7ff000)
              | ((pt & 0x000c00)<<13);
        mask = 0x7ffc00;
        break;
      case MEMC_PAGESIZE_1_8K:
        phys = ((pt>>1) & 0x3f) | ((pt & 1) << 6);
        logadr = (pt & 0x7fe000)
              | ((pt & 0x000c00)<<13);
        mask = 0x7fec00;
        break;
      case MEMC_PAGESIZE_2_16K:
        phys = ((pt>>2) & 0x1f) | ((pt & 3) << 5);
        logadr = (pt & 0x7fc000)
              | ((pt & 0x000c00)<<13);
        mask = 0x7fcc00;
        break;
      case MEMC_PAGESIZE_3_32K:
        phys = ((pt>>3) & 0xf) | ((pt&1)<<4) | ((pt&2)<<5) | ((pt&4)<<3) | (pt&0x80) | ((pt>>4)&0x100);
        logadr = (pt & 0x7f8000)
              | ((pt & 0x000c00)<<13);
        mask = 0x7f8c00;
        break;
    }
    ARMword size=1<<(12+MEMC.PageSizeFlags);
    phys *= size;
    FastMapEntry *entry = FastMap_GetEntryNoWrap(logadr);
    
    /* To cope with multiply mapped pages (i.e. multiple physical pages mapping to the same logical page) we need to check if the page we're about to unmap is still owned by us
       If it is owned by us, we'll have to search the page tables for a replacement (if any)
       Otherwise we don't need to do anything at all
       Note that we only need to check the first page for ownership, because any change to the page size will result in the full map being rebuilt */
       
    FastMapUInt addr = ((FastMapUInt)(MEMC.PhysRam+(phys>>2)))-logadr; /* Address part of FlagsAndData */
    if((entry->FlagsAndData<<8) == addr)
    {
      /* We own this page */
      ARMword idx2;
      pt &= mask;
      for(idx2=0;idx2<512;idx2++)
      {
        if(idx2 != idx)
        {
          int pt2 = MEMC.PageTable[idx2];
          if((pt2 > 0) && ((pt2 & mask) == pt))
          {
            /* We've found a suitable replacement */
            ARMul_RebuildFastMapPTIdx(idx2); /* Take the easy way out */
            return;
          }
        }
      }
      /* No replacement found, so just nuke this entry */
      while(size) {
        if((entry->FlagsAndData<<8) == addr)
          entry->FlagsAndData = 0; /* No need to nuke function pointer */
        entry++;
        size -= 4096;
      }
    }    
  }
}

static void FastMap_DMAAbleWrite(ARMword address,ARMword data)
{
  MEMC.UpdateFlags[address/UPDATEBLOCKSIZE]++;
#ifdef DIRECT_DISPLAY
#ifdef __riscos__
  if (MEMC.Vstart!=Vstartold || MEMC.Vend!=Vendold)
  {
    int size=_swi(OS_ReadDynamicArea, _IN(0)|_RETURN(1), 2);
    int size_wanted=(MEMC.Vend+1-MEMC.Vstart)*16;

    printf("Size wanted: %d, Old size: %d\n",size_wanted,size);

    size_wanted >>= (3-MonitorBpp);

    _swix(OS_ChangeDynamicArea, _INR(0,1), 2, size_wanted - size);

    size = _swi(OS_ReadDynamicArea, _IN(0) | _RETURN(1), 2);

    printf("Size wanted: %d, New size: %d\n", size_wanted, size);

    {
      int block[3];

      block[0]=148;
      block[1]=150;
      block[2]=-1;

      _swi(OS_ReadVduVariables, _INR(0,1), &block, &block);

      DirectScreenMemory = (int *)block[0];
      DirectScreenExtent = block[1];
    }

    Vstartold=MEMC.Vstart;
    Vendold=MEMC.Vend;
  }
#endif

  if (MEMC.Vinit!=Vinitold)
  {
    unsigned int offset=(MEMC.Vinit-MEMC.Vstart)*16;

    offset >>= (3-MonitorBpp);

#ifdef __riscos__
    {
        char block[5];
        block[0]=(1<<1)+(1<<0);
        block[1]=(offset & 0x000000ff);
        block[2]=(offset & 0x0000ff00) >> 8;
        block[3]=(offset & 0x00ff0000) >> 16;
        block[4]=(offset & 0xff000000) >> 24;

        _swi(OS_Word, _INR(0,1), 22, &block);
    }
#endif
#ifdef SYSTEM_gp2x
    {
        extern void gp2xScreenOffset(int);
        gp2xScreenOffset(offset);
    }
#endif

    Vinitold=MEMC.Vinit;
  }

  if (address < DirectScreenExtent) {
//    extern void ScreenWrite(int *address, ARMword data);

//    ScreenWrite(DirectScreenMemory + address / 4, data);
    DirectScreenMemory[address/4] = data;
  }
#endif
}

static ARMword FastMap_LogRamFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* Write to DMAAble log RAM, kinda crappy */
  ARMword *phy = FastMap_Log2Phy(FastMap_GetEntry(addr),addr&~3);
  if(flags & FASTMAP_ACCESSFUNC_BYTE)
  {
    ARMword shift = ((addr&3)<<3);
    data = (data&0xff)<<shift;
    data |= (*phy) &~ (0xff<<shift);
  }
  *phy = data;
  *(FastMap_Phy2Func(phy)) = FASTMAP_CLOBBEREDFUNC;
  /* Convert pointer to physical addr, then update DMA flags */
  addr = (ARMword) (((FastMapUInt)phy)-((FastMapUInt)MEMC.PhysRam));
  FastMap_DMAAbleWrite(addr,data);
} 

static void ARMul_RebuildFastMapPTIdx(ARMword idx)
{
  if(MEMC.ROMMapFlag)
    return; /* Still in ROM mode, abort */

  static const FastMapUInt PPL_To_Flags[4] = {
  FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_USR|FASTMAP_W_OS|FASTMAP_W_SVC, /* PPL 00 */
  FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_OS|FASTMAP_W_SVC,  /* PPL 01 */
  FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_SVC,  /* PPL 10 */
  FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_SVC,  /* PPL 11 */
  };

  int pt = MEMC.PageTable[idx];
  if(pt>0)
  {
    ARMword logadr,phys;
    switch(MEMC.PageSizeFlags) {
      case MEMC_PAGESIZE_O_4K:
        phys = pt & 127;
        logadr = (pt & 0x7ff000)
              | ((pt & 0x000c00)<<13);
        break;
      case MEMC_PAGESIZE_1_8K:
        phys = ((pt>>1) & 0x3f) | ((pt & 1) << 6);
        logadr = (pt & 0x7fe000)
              | ((pt & 0x000c00)<<13);
        break;
      case MEMC_PAGESIZE_2_16K:
        phys = ((pt>>2) & 0x1f) | ((pt & 3) << 5);
        logadr = (pt & 0x7fc000)
              | ((pt & 0x000c00)<<13);
        break;
      case MEMC_PAGESIZE_3_32K:
        phys = ((pt>>3) & 0xf) | ((pt&1)<<4) | ((pt&2)<<5) | ((pt&4)<<3) | (pt&0x80) | ((pt>>4)&0x100);
        logadr = (pt & 0x7f8000)
              | ((pt & 0x000c00)<<13);
        break;
    }
    ARMword size=1<<(12+MEMC.PageSizeFlags);
    phys *= size;
    ARMword flags = PPL_To_Flags[(pt>>8)&3];
    if(phys<512*1024)
    {
      /* DMAable, must use func on write */
      FastMap_SetEntries(logadr,MEMC.PhysRam+(phys>>2),FastMap_LogRamFunc,flags|FASTMAP_W_FUNC,size);
    }
    else
    {
      /* Normal */
      FastMap_SetEntries(logadr,MEMC.PhysRam+(phys>>2),0,flags,size);
    }
  }
}

static void DMA_PutVal(ARMul_State *state,ARMword address)
{
    /* DMA address generation - MEMC Control reg */
    ARMword RegNum,RegVal;

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
        ioc.IRQStatus &= ~IRQB_SIRQ; /* Take sound interrupt off */
        IO_UpdateNirq(state);
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
          ioc.IRQStatus |= IRQB_SIRQ; /* Take sound interrupt on */
          IO_UpdateNirq(state);
        }
        break;

      case 7: /* MEMC Control register */
        dbug_memc("MEMC Control register set to 0x%x by PC=0x%x R[15]=0x%x\n",
                  address, (unsigned int)state->pc, (unsigned int)state->Reg[15]);
        MEMC.ControlReg = address;
        MEMC.PageSizeFlags = (MEMC.ControlReg & 12) >> 2;
        FastMap_RebuildMapMode(state);
        ARMul_RebuildFastMap();
        break;
    }
}

static void MEMC_PutVal(ARMword address)
{
    /* Logical-to-physical address translation */
    unsigned tmp;

    tmp = address - MEMORY_0x3800000_W_LOG2PHYS;

    address = ((address >> 4) & 0x100) | (address & 0xff);

    ARMul_PurgeFastMapPTIdx(address); /* Unmap old value */
    MEMC.PageTable[address] = tmp & 0x0fffffff;
    ARMul_RebuildFastMapPTIdx(address); /* Map in new value */
}

static ARMword FastMap_ROMMap1Func(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* Does nothing more than set ROMMapFlag to 2 and read a word of ROM */
  MEMC.ROMMapFlag = 2;
  ARMul_RebuildFastMap();
  data = MEMC.ROMHigh[(addr & MEMC.ROMHighMask)>>2];
  if(flags & FASTMAP_ACCESSFUNC_BYTE)
    data = (data>>((addr&3)<<3))&0xff;
  return data;
}

static ARMword FastMap_PhysRamFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* Read/write from phys RAM, switching to ROMMapFlag 0 if necessary */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap();
  }
  addr -= MEMORY_0x2000000_RAM_PHYS;
  if(addr >= MEMC.RAMSize)
    return 0;
  ARMword *phy = &MEMC.PhysRam[addr>>2];
  switch(flags & (FASTMAP_ACCESSFUNC_WRITE|FASTMAP_ACCESSFUNC_BYTE))
  {
  case 0:
    return *phy;
  case FASTMAP_ACCESSFUNC_BYTE:
    return ((*phy)>>((addr&3)<<3))&0xff;
  case FASTMAP_ACCESSFUNC_WRITE|FASTMAP_ACCESSFUNC_BYTE:
    {
      ARMword shift = ((addr&3)<<3);
      phy = (ARMword*)(((FastMapUInt)phy)&~3);
      data = (data&0xff)<<shift;
      data |= (*phy) &~ (0xff<<shift);
    }
    /* fall through... */
  case FASTMAP_ACCESSFUNC_WRITE:
    *phy = data;
    *(FastMap_Phy2Func(phy)) = FASTMAP_CLOBBEREDFUNC;
    if(addr < 512*1024)
      FastMap_DMAAbleWrite(addr,data);
    return 0;
  }
}

static ARMword FastMap_DummyROMMap2Func(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* Does nothing except switch to ROMMapFlag 0 if necessary */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap();
  }
  return 0;
}

static ARMword FastMap_ConIOFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* Read/write IO space */
  if((flags & (FASTMAP_ACCESSFUNC_WRITE|FASTMAP_ACCESSFUNC_STATECHANGE)) == FASTMAP_ACCESSFUNC_WRITE)
    return 0; /* Write without state change is bad! */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap();
  }
  if(flags & FASTMAP_ACCESSFUNC_WRITE)
  {
    PutValIO(state,addr,data,flags&FASTMAP_ACCESSFUNC_BYTE);
    return 0;
  }
  data = GetWord_IO(state,addr);
  if(flags & FASTMAP_ACCESSFUNC_BYTE)
    data = (data>>((addr&3)<<3))&0xff;
  return data;
}

static ARMword FastMap_VIDCFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* May be a ROMMapFlag read */
  if((flags & (FASTMAP_ACCESSFUNC_WRITE|FASTMAP_ACCESSFUNC_STATECHANGE)) == FASTMAP_ACCESSFUNC_WRITE)
    return 0; /* Write without state change is bad! */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap();
  }
  if(flags & FASTMAP_ACCESSFUNC_WRITE)
  {
    VIDC_PutVal(state,addr,data,flags&FASTMAP_ACCESSFUNC_BYTE);
    return 0;
  }
  if(MEMC.ROMLow)
  {
    data = MEMC.ROMLow[(addr & MEMC.ROMLowMask)>>2];
    if(flags & FASTMAP_ACCESSFUNC_BYTE)
      data = (data>>((addr&3)<<3))&0xff;
  }
  return data;
}

static ARMword FastMap_DMAFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* May be a ROMMapFlag read */
  if((flags & (FASTMAP_ACCESSFUNC_WRITE|FASTMAP_ACCESSFUNC_STATECHANGE)) == FASTMAP_ACCESSFUNC_WRITE)
    return 0; /* Write without state change is bad! */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap();
  }
  if(flags & FASTMAP_ACCESSFUNC_WRITE)
  {
    DMA_PutVal(state,addr);
    return 0;
  }
  if(MEMC.ROMLow)
  {
    data = MEMC.ROMLow[(addr & MEMC.ROMLowMask)>>2];
    if(flags & FASTMAP_ACCESSFUNC_BYTE)
      data = (data>>((addr&3)<<3))&0xff;
  }
  return data;
}

static ARMword FastMap_MEMCFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* May be a ROMMapFlag read */
  if((flags & (FASTMAP_ACCESSFUNC_WRITE|FASTMAP_ACCESSFUNC_STATECHANGE)) == FASTMAP_ACCESSFUNC_WRITE)
    return 0; /* Write without state change is bad! */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap();
  }
  if(flags & FASTMAP_ACCESSFUNC_WRITE)
  {
    MEMC_PutVal(addr);
    return 0;
  }
  if(MEMC.ROMHigh)
  {
    data = MEMC.ROMHigh[(addr & MEMC.ROMHighMask)>>2];
    if(flags & FASTMAP_ACCESSFUNC_BYTE)
      data = (data>>((addr&3)<<3))&0xff;
  }
  return data;
}

static void ARMul_RebuildFastMap(void)
{
  ARMword i;
  
  /* completely rebuild the fast map */
  switch(MEMC.ROMMapFlag)
  {
  case 0:
    {
      /* Map in logical RAM using the page tables */
      FastMap_SetEntries(0,0,0,0,0x2000000);

      for(i=0;i<512;i++)
        ARMul_RebuildFastMapPTIdx(i);
    }
    break;
  case 1:
    /* Map ROM to 0x0, using access func, even though we know the very first thing the processor will do is fetch from 0x0 and transition us away... */
    FastMap_SetEntries_Repeat(0,0,FastMap_ROMMap1Func,FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_R_FUNC,MEMC.ROMHighSize,0x800000);
    FastMap_SetEntries(0x800000,0,0,0,0x1800000);
    break;
  case 2:
    /* Map ROM to 0x0 */
    FastMap_SetEntries_Repeat(0,MEMC.ROMHigh,0,FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC,MEMC.ROMHighSize,0x800000);
    FastMap_SetEntries(0x800000,0,0,0,0x1800000);
    break;
  }

  /* Map physical RAM */
  if(MEMC.ROMMapFlag == 2)
  {
    /* Use access func for all of it */
    FastMap_SetEntries(MEMORY_0x2000000_RAM_PHYS,0,FastMap_PhysRamFunc,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_R_FUNC|FASTMAP_W_FUNC,MEMC.RAMSize);
  }
  else
  {
    /* Lower 512K must use access func for write */
    ARMword low = (MEMC.RAMSize>512*1024?512*1024:MEMC.RAMSize);
    FastMap_SetEntries(MEMORY_0x2000000_RAM_PHYS,MEMC.PhysRam,FastMap_PhysRamFunc,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_W_FUNC,low);
    if(low < MEMC.RAMSize)
    {
      /* Remainder can use direct access for read/write */
      FastMap_SetEntries(MEMORY_0x2000000_RAM_PHYS+512*1024,MEMC.PhysRam+(512*1024>>2),0,FASTMAP_R_SVC|FASTMAP_W_SVC,MEMC.RAMSize-512*1024);
    }
  }
  /* Unused RAM space doesn't do much */
  if(MEMC.RAMSize < 16*1024*1024)
    FastMap_SetEntries(MEMORY_0x2000000_RAM_PHYS+MEMC.RAMSize,0,FastMap_DummyROMMap2Func,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_R_FUNC|FASTMAP_W_FUNC,16*1024*1024-MEMC.RAMSize);

  /* I/O space */
  FastMap_SetEntries(MEMORY_0x3000000_CON_IO,0,FastMap_ConIOFunc,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_R_FUNC|FASTMAP_W_FUNC,0x400000);

  /* ROM Low/VIDC/DMA */
  /* Make life easier for ourselves by mapping in everything as DMA then overwriting with VIDC */
  if(MEMC.ROMLow && (MEMC.ROMMapFlag != 2))
    FastMap_SetEntries_Repeat(MEMORY_0x3400000_R_ROM_LOW,MEMC.ROMLow,FastMap_DMAFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC,MEMC.ROMLowSize,0x400000);
  else
    FastMap_SetEntries(MEMORY_0x3400000_R_ROM_LOW,0,FastMap_DMAFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC|FASTMAP_R_FUNC,0x400000);

  /* Overwrite with VIDC */
  FastMapEntry *entry = FastMap_GetEntryNoWrap(MEMORY_0x3400000_W_VIDEOCON);
  for(i=0;i<MEMORY_0x3600000_W_DMA_GEN-MEMORY_0x3400000_W_VIDEOCON;i+=4096)
  {
    entry->AccessFunc = FastMap_VIDCFunc;
    entry++;
  }

  /* ROM High/MEMC */
  if(MEMC.ROMHigh && (MEMC.ROMMapFlag != 2))
    FastMap_SetEntries_Repeat(MEMORY_0x3800000_R_ROM_HIGH,MEMC.ROMHigh,FastMap_MEMCFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC,MEMC.ROMHighSize,0x800000);
  else
    FastMap_SetEntries(MEMORY_0x3800000_R_ROM_HIGH,0,FastMap_MEMCFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC|FASTMAP_R_FUNC,0x800000);
}

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
SoundDMAFetch(SoundData *buffer, ARMul_State *state)
{
  int i;
  if ((MEMC.ControlReg & (1 << 11)) == 0) {
    return 1;
  }

  if(numberOfChannels==0)
    SoundUpdateStereoImage(state);

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

      ioc.IRQStatus |= IRQB_SIRQ; /* Take sound interrupt on */
      IO_UpdateNirq(state);

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
SoundUpdateStereoImage(ARMul_State *state)
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

  SoundUpdateSampleRate(state);
}

void
SoundUpdateSampleRate(ARMul_State *state)
{
  /* SFR = (N - 2) where N is sample period in microseconds. */
  if (numberOfChannels != 8) {
    sound_setSampleRate(VIDC.SoundFreq + 2);
  } else {
    sound_setSampleRate((VIDC.SoundFreq + 2) * 8);
  }
}
#endif

#ifndef FASTMAP_INLINE
#include "arch/fastmap.c"
#endif
