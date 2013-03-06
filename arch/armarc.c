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

#include "../armdefs.h"
#include "../hostfs.h"

#include "dbugsys.h"

#include "armarc.h"
#include "archio.h"
#include "archio.h"
#include "fdc1772.h"
#include "ReadConfig.h"
#include "Version.h"
#include "extnrom.h"
#include "ArcemConfig.h"
#include "sound.h"
#include "displaydev.h"
#include "filecalls.h"
#include "ControlPane.h"


#ifdef MACOSX
#include <unistd.h>
extern char arcemDir[256];
#endif

/* Page size flags */
#define MEMC_PAGESIZE_O_4K     0
#define MEMC_PAGESIZE_1_8K     1
#define MEMC_PAGESIZE_2_16K    2
#define MEMC_PAGESIZE_3_32K    3


struct MEMCStruct memc;

/*-----------------------------------------------------------------------------*/

static ARMword ARMul_ManglePhysAddr(ARMword phy);

/*------------------------------------------------------------------------------*/
/* OK - this is getting treated as an odds/sods engine - just hook up anything
   you need to do occasionally! */
static void DumpHandler(int sig) {
  ARMul_State *state = &statestr;
  FILE *res;
  int i, idx;
  ARMword size;

  fprintf(stderr,"SIGUSR2 at PC=0x%x\n",ARMul_GetPC(state));
#ifndef WIN32
  signal(SIGUSR2,DumpHandler);
#endif
  /* Register dump */
  fprintf(stderr, "Current registers:\n"
                  "r0  = %08x  r1  = %08x  r2  = %08x  r3  = %08x\n"
                  "r4  = %08x  r5  = %08x  r6  = %08x  r7  = %08x\n"
                  "r8  = %08x  r9  = %08x  r10 = %08x  r11 = %08x\n"
                  "r12 = %08x  r13 = %08x  r14 = %08x  r15 = %08x\n",
    state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
    state->Reg[4], state->Reg[5], state->Reg[6], state->Reg[7],
    state->Reg[8], state->Reg[9], state->Reg[10], state->Reg[11],
    state->Reg[12], state->Reg[13], state->Reg[14], state->Reg[15]);
  if(state->Bank == FIQBANK)
  {
    fprintf(stderr,"USR registers:\n"
                   "r8  = %08x  r9  = %08x  r10 = %08x  r11 = %08x\n"
                   "r12 = %08x  r13 = %08x  r14 = %08x\n",
      state->RegBank[USERBANK][8], state->RegBank[USERBANK][9],
      state->RegBank[USERBANK][10], state->RegBank[USERBANK][11],
      state->RegBank[USERBANK][12],
      state->RegBank[USERBANK][13], state->RegBank[USERBANK][14]);
  }
  else if(state->Bank != USERBANK)
  {
    fprintf(stderr,"USR registers:  r13 = %08x  r14 = %08x\n",
      state->RegBank[USERBANK][13], state->RegBank[USERBANK][14]);
  }
  if(state->Bank != FIQBANK)
  {
    fprintf(stderr,"FIQ registers:\n"
                   "r8  = %08x  r9  = %08x  r10 = %08x  r11 = %08x\n"
                   "r12 = %08x  r13 = %08x  r14 = %08x\n",
      state->RegBank[FIQBANK][8], state->RegBank[FIQBANK][9],
      state->RegBank[FIQBANK][10], state->RegBank[FIQBANK][11],
      state->RegBank[FIQBANK][12],
      state->RegBank[FIQBANK][13], state->RegBank[FIQBANK][14]);
  }
  if(state->Bank != IRQBANK)
  {
    fprintf(stderr,"IRQ registers:  r13 = %08x  r14 = %08x\n",
      state->RegBank[IRQBANK][13], state->RegBank[IRQBANK][14]);
  }  
  if(state->Bank != SVCBANK)
  {
    fprintf(stderr,"SVC registers:  r13 = %08x  r14 = %08x\n",
      state->RegBank[SVCBANK][13], state->RegBank[SVCBANK][14]);
  }  

  /* IOC timers */
  for(i=0;i<4;i++)
    fprintf(stderr,"Timer%d Count %08x Latch %08x\n",i,ioc.TimerCount[i],ioc.TimerInputLatch[i]);

  /* Memory map */
  fprintf(stderr,"MEMC using %dKB page size\n",4<<MEMC.PageSizeFlags);
  size=1<<(12+MEMC.PageSizeFlags);
  for(idx=0;idx<512;idx++)
  {
    int32_t pt = MEMC.PageTable[idx];
    if(pt>0)
    {
      ARMword logadr,phys,mangle;
      /* Assume MEMC isn't in OS mode */
      static const char *prot[4] = {"USR R/W","USR R","SVC only","SVC only"};
      switch(MEMC.PageSizeFlags) {
        default:
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
      phys *= size;
      mangle = ARMul_ManglePhysAddr(phys);
      fprintf(stderr,"log %08x -> phy %08x (pre-mangle %08x) prot %s\n",logadr,mangle,phys,prot[(pt>>8)&3]);
    }
  }

  /* Physical RAM dump */
#ifdef __riscos__
  res=fopen("<arcem$dir>.memdump","wb");
#else
  res=fopen("memdump","wb");
#endif

  if (res==NULL) {
    fprintf(stderr,"Could not open memory dump file\n");
    return;
  };

  fwrite(MEMC.PhysRam,1,MEMC.RAMSize,res);

  fclose(res);

#ifdef __riscos__
  __write_backtrace(sig);
#endif

  exit(0);
} /* DumpHandler */

/**
 * ARMul_MemoryInit
 *
 * Initialise the memory interface, Initialise memory map, load Rom,
 * load extension Roms
 *
 * @param state
 * @returns
 */
unsigned
ARMul_MemoryInit(ARMul_State *state)
{
  ARMword RAMChunkSize;
  FILE *ROMFile;
  int PresPage;
  unsigned int i;
  uint32_t extnrom_size = 0;
#if defined(EXTNROM_SUPPORT)
  uint32_t extnrom_entry_count;
#endif
  uint32_t initmemsize = 0;
  
  MEMC.DRAMPageSize = MEMC_PAGESIZE_3_32K;
  switch(hArcemConfig.eMemSize) {
    case MemSize_256K:
#if 0
      initmemsize = 256 * 1024;
      MEMC.DRAMPageSize = MEMC_PAGESIZE_O_4K;
      break;
#else
      fprintf(stderr, "256K memory size may not be working right. Rounding up to 512K\n");
      /* fall through... */
#endif
      
    case MemSize_512K:
      initmemsize = 512 * 1024;
      MEMC.DRAMPageSize = MEMC_PAGESIZE_O_4K;
      break;

    case MemSize_1M:
      initmemsize = 1 * 1024 * 1024;
      MEMC.DRAMPageSize = MEMC_PAGESIZE_1_8K;
      break;

    case MemSize_2M:
      initmemsize = 2 * 1024 * 1024;
      MEMC.DRAMPageSize = MEMC_PAGESIZE_2_16K;
      break;

    case MemSize_4M:
      initmemsize = 4 * 1024 * 1024;
      break;

    case MemSize_8M:
      initmemsize = 8 * 1024 * 1024;
      break;

    case MemSize_12M:
      initmemsize = 12 * 1024 * 1024;
      break;

    case MemSize_16M:
      initmemsize = 16 * 1024 * 1024;
      break;

    default:
      ControlPane_Error(EXIT_FAILURE,"Unsupported memory size");
  }

  dbug("Reading config file....\n");
  ReadConfigFile(state);

#ifndef WIN32
  signal(SIGUSR2,DumpHandler);
#endif

  /* Top bit set means it isn't valid */
  for (PresPage = 0; PresPage < 512; PresPage++)
    MEMC.PageTable[PresPage] = 0xffffffff;

  MEMC.ROMMapFlag    = 1; /* Map ROM to address 0 */
  MEMC.ControlReg    = 0; /* Defaults */
  MEMC.PageSizeFlags = MEMC_PAGESIZE_O_4K;
  MEMC.NextSoundBufferValid = false; /* Not set till Sstart and SendN have been written */

  MEMC.RAMSize = initmemsize;
  MEMC.RAMMask = (initmemsize-1) & (4*1024*1024-1); /* Mask within a 4M MEMC bank */

#ifdef MACOSX
  {
    chdir(arcemDir);
  }
#endif
  if (ROMFile = fopen(hArcemConfig.sRomImageName, "rb"), ROMFile == NULL) {
    ControlPane_Error(2,"Couldn't open ROM file");
  }

  /* Find the rom file size */
  fseek(ROMFile, 0l, SEEK_END);

  MEMC.ROMHighSize = (((ARMword) ftell(ROMFile))+4095)&~4095;
  MEMC.ROMHighMask = MEMC.ROMHighSize-1;

  if(MEMC.ROMHighSize & MEMC.ROMHighMask) {
    ControlPane_Error(3,"ROM High isn't power of 2 in size\n");
  }

  fseek(ROMFile, 0l, SEEK_SET);

#if defined(EXTNROM_SUPPORT)
  /* Add the space required by an Extension Rom */
  extnrom_size = (extnrom_calculate_size(&extnrom_entry_count)+4095)&~4095;
  fprintf(stderr, "extnrom_size = %u, extnrom_entry_count= %u\n",
          extnrom_size, extnrom_entry_count);
#endif /* EXTNROM_SUPPORT */
  dbug("Total ROM size required = %u KB\n",
       (MEMC.ROMHighSize + extnrom_size) / 1024);

  /* Now allocate ROMs & RAM in one chunk */
  RAMChunkSize = MAX(MEMC.RAMSize,512*1024); /* Ensure at least 512K RAM allocated to avoid any issues caused by DMA pointers going out of range */
  MEMC.ROMRAMChunk = calloc(RAMChunkSize+MEMC.ROMHighSize+extnrom_size+256,1);
  MEMC.EmuFuncChunk = calloc(RAMChunkSize+MEMC.ROMHighSize+extnrom_size+256,sizeof(FastMapUInt)/4);
  if((MEMC.ROMRAMChunk == NULL) || (MEMC.EmuFuncChunk == NULL)) {
    ControlPane_Error(3,"Couldn't allocate ROMRAMChunk/EmuFuncChunk\n");
  }
#ifdef FASTMAP_64
  /* On 64bit systems, ROMRAMChunk needs shifting to account for the shift that occurs in FastMap_Phy2Func */
  state->FastMapInstrFuncOfs = ((FastMapUInt)MEMC.EmuFuncChunk)-(((FastMapUInt)MEMC.ROMRAMChunk)<<1);
#else
  state->FastMapInstrFuncOfs = ((FastMapUInt)MEMC.EmuFuncChunk)-((FastMapUInt)MEMC.ROMRAMChunk);
#endif
  /* Get everything 256 byte aligned for FastMap to work */
  MEMC.PhysRam = (ARMword*) ((((FastMapUInt)MEMC.ROMRAMChunk)+255)&~255); /* RAM must come first for FastMap_LogRamFunc to work! */
  MEMC.ROMHigh = MEMC.PhysRam + (RAMChunkSize>>2);

  dbug(" Loading ROM....\n ");

  File_ReadEmu(ROMFile,(uint8_t *) MEMC.ROMHigh,MEMC.ROMHighSize);

  /* Close System ROM Image File */
  fclose(ROMFile);

  /* Create Space for extension ROM in ROMLow */
  if (extnrom_size) {
    MEMC.ROMLowSize = extnrom_size;
    MEMC.ROMLowMask = MEMC.ROMLowSize-1;
  
    if(MEMC.ROMLowSize & MEMC.ROMLowMask) {
      ControlPane_Error(3,"ROM Low isn't power of 2 in size\n");
    }

    /* calloc() now used to ensure that Extension ROM space is zero'ed */
    MEMC.ROMLow = MEMC.ROMHigh + (MEMC.ROMHighSize>>2);

#if defined(EXTNROM_SUPPORT)
    /* Load extension ROM */
    dbug("Loading Extension ROM...\n");
    extnrom_load(extnrom_size, extnrom_entry_count, MEMC.ROMLow);
#endif /* EXTNROM_SUPPORT */
  }


  dbug(" ..Done\n ");

  IO_Init(state);

  if (DisplayDev_Init(state)) {
    /* There was an error of some sort - it will already have been reported */
    ControlPane_Error(EXIT_FAILURE,"Could not initialise display - exiting\n");
  }

  if (Sound_Init(state)) {
    /* There was an error of some sort - it will already have been reported */
    ControlPane_Error(EXIT_FAILURE,"Could not initialise sound output - exiting\n");
  }

  for (i = 0; i < 512 * 1024 / UPDATEBLOCKSIZE; i++) {
    MEMC.UpdateFlags[i] = 1;
  }

  ARMul_RebuildFastMap(state);
  FastMap_RebuildMapMode(state);

#ifdef HOSTFS_SUPPORT
  hostfs_init();
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
  free(MEMC.ROMRAMChunk);
  free(MEMC.EmuFuncChunk);
  free(state->Display);
}

static ARMword ARMul_ManglePhysAddr(ARMword phy)
{
  /* Emulate the different ways that MEMC converts physical addresses to
     row & column addresses. We perform two mappings here: From the physical
     address to row & column, via the current MEMC page size, and then from
     the row & column back to a physical address, via our emulated DRAM page
     size.

     Assuming the OS detects the right amount of RAM, this will be a 1:1
     mapping, allowing all our DMA operations (video, audio) to work the same
     as before. But while the OS (in this case RISC OS) is doing its DRAM
     detection it won't be a 1:1 mapping, but the mapping will be such that
     when the OS tries an address that should be invalid it will generate an
     invalid address here too.

     Note that physical address bits 0-11, 14-18 & 22-23 don't get affected by
     the mangling. This corresponds to row bits 0-7 and column bits 0-1, 4-8.
     So all we need to do is calculate row bits 8,9 and column bits 2,3,9.
  */
  ARMword bank;

  if(MEMC.PageSizeFlags != MEMC.DRAMPageSize)
  {
    ARMword b12=0,b13=0,b19=0,b20=0,b21=0;
    ARMword r8=0,r9=0,c2=0,c3=0,c9=0;
  
    switch(MEMC.PageSizeFlags)
    {
    case MEMC_PAGESIZE_O_4K:
      c2 = (phy>>12) & 1;
      c3 = (phy>>13) & 1;
      break;
    case MEMC_PAGESIZE_1_8K:
      r8 = (phy>>12) & 1;
      c2 = (phy>>19) & 1;
      c3 = (phy>>13) & 1;
      break;
    case MEMC_PAGESIZE_2_16K:
      r8 = (phy>>12) & 1;
      c2 = (phy>>19) & 1;
      c3 = (phy>>13) & 1;
      c9 = (phy>>20) & 1;
      break;
    case MEMC_PAGESIZE_3_32K:
      r8 = (phy>>12) & 1;
      r9 = (phy>>13) & 1;
      c2 = (phy>>19) & 1;
      c3 = (phy>>21) & 1;
      c9 = (phy>>20) & 1;
      break;
    }
  
    phy &= 0xffc7cfff; /* Mask off the bits extracted above */
  
    /* Now translate back */
  
    switch(MEMC.DRAMPageSize)
    {
    case MEMC_PAGESIZE_O_4K:
      b12 = c2;
      b13 = c3;
      break;
    case MEMC_PAGESIZE_1_8K:
      b12 = r8;
      b19 = c2;
      b13 = c3;
      break;
    case MEMC_PAGESIZE_2_16K:
      b12 = r8;
      b19 = c2;
      b13 = c3;
      b20 = c9;
      break;
    case MEMC_PAGESIZE_3_32K:
      b12 = r8;
      b13 = r9;
      b19 = c2;
      b21 = c3;
      b20 = c9;
      break;
    }

    phy |= (b12<<12) | (b13<<13) | (b19<<19) | (b20<<20) | (b21<<21);
  }

  /* Mask by how much RAM is fitted
     We use slightly funny masking logic to deal with the case of having 12M
     RAM fitted (3 banks of 4M). Any banks outside the number which are fitted
     will wrap back to bank 0.
  */

  bank = phy>>22;

  phy &= MEMC.RAMMask;

  if(bank >= MEMC.RAMSize>>22)
    bank = 0;

  return phy | (bank<<22);
}    

static void FastMap_SetEntries(ARMul_State *state, ARMword addr,ARMword *data,FastMapAccessFunc func,FastMapUInt flags,ARMword size)
{
  FastMapEntry *entry = FastMap_GetEntryNoWrap(state,addr);
//  fprintf(stderr,"FastMap_SetEntries(%08x,%08x,%08x,%08x,%08x)\n",addr,data,func,flags,size);
  FastMapUInt offset = ((FastMapUInt)data)-addr; /* Offset so we can just add the phy addr to get a pointer back */
  flags |= offset>>8;
//  fprintf(stderr,"->entry %08x\n->FlagsAndData %08x\n",entry,flags);
  while(size) {
    entry->FlagsAndData = flags;
    entry->AccessFunc = func;
    entry++;
    size -= 4096;
  }
}

static void FastMap_SetEntries_Repeat(ARMul_State *state,ARMword addr,ARMword *data,FastMapAccessFunc func,FastMapUInt flags,ARMword size,ARMword totsize)
{
  while(totsize > size) {
    FastMap_SetEntries(state,addr,data,func,flags,size);
    addr += size;
    totsize -= size;
  }
  /* Should always be something left */
  FastMap_SetEntries(state,addr,data,func,flags,totsize);
}

static void ARMul_RebuildFastMapPTIdx(ARMul_State *state, ARMword idx);

static void ARMul_PurgeFastMapPTIdx(ARMul_State *state,ARMword idx)
{
  FastMapEntry *entry;
  FastMapUInt addr;
  ARMword size;
  int32_t pt;
  if(MEMC.ROMMapFlag)
    return; /* Still in ROM mode, abort */
    
  pt = MEMC.PageTable[idx];
  if(pt>0)
  {
    ARMword logadr,phys,mask;
    switch(MEMC.PageSizeFlags) {
      default:
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
    size=12+MEMC.PageSizeFlags;
    phys = ARMul_ManglePhysAddr(phys<<size);
    size = 1<<size;
    entry = FastMap_GetEntryNoWrap(state,logadr);
    
    /* To cope with multiply mapped pages (i.e. multiple physical pages mapping to the same logical page) we need to check if the page we're about to unmap is still owned by us
       If it is owned by us, we'll have to search the page tables for a replacement (if any)
       Otherwise we don't need to do anything at all
       Note that we only need to check the first page for ownership, because any change to the page size will result in the full map being rebuilt */
       
    addr = ((FastMapUInt)(MEMC.PhysRam+(phys>>2)))-logadr; /* Address part of FlagsAndData */
    if((entry->FlagsAndData<<8) == addr)
    {
      /* We own this page */
      ARMword idx2;
      pt &= mask;
      for(idx2=0;idx2<512;idx2++)
      {
        if(idx2 != idx)
        {
          int32_t pt2 = MEMC.PageTable[idx2];
          if((pt2 > 0) && ((pt2 & mask) == pt))
          {
            /* We've found a suitable replacement */
            ARMul_RebuildFastMapPTIdx(state, idx2); /* Take the easy way out */
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
}

static ARMword FastMap_LogRamFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* Write to DMAAble log RAM, kinda crappy */
  ARMword *phy = FastMap_Log2Phy(FastMap_GetEntry(state,addr),addr&~3);
  ARMword orig = *phy;
  if(flags & FASTMAP_ACCESSFUNC_BYTE)
  {
    ARMword shift = ((addr&3)<<3);
    data = (data&0xff)<<shift;
    data |= orig &~ (0xff<<shift);
  }
  if(orig != data)
  {
    *phy = data;
    *(FastMap_Phy2Func(state,phy)) = FASTMAP_CLOBBEREDFUNC;
    /* Convert pointer to physical addr, then update DMA flags */
    addr = (ARMword) (((FastMapUInt)phy)-((FastMapUInt)MEMC.PhysRam));
    FastMap_DMAAbleWrite(addr,data);
  }
  return 0;
} 

static void ARMul_RebuildFastMapPTIdx(ARMul_State *state, ARMword idx)
{
  int32_t pt;
  ARMword size;
  FastMapUInt flags;

  static const FastMapUInt PPL_To_Flags[4] = {
  FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_USR|FASTMAP_W_OS|FASTMAP_W_SVC, /* PPL 00 */
  FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_OS|FASTMAP_W_SVC,  /* PPL 01 */
  FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_SVC,  /* PPL 10 */
  FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_W_SVC,  /* PPL 11 */
  };

  if(MEMC.ROMMapFlag)
    return; /* Still in ROM mode, abort */

  pt = MEMC.PageTable[idx];
  if(pt>0)
  {
    ARMword logadr,phys;
    switch(MEMC.PageSizeFlags) {
      default:
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
    size=12+MEMC.PageSizeFlags;
    phys = ARMul_ManglePhysAddr(phys<<size);
    size = 1<<size;
    flags = PPL_To_Flags[(pt>>8)&3];
    if((phys<512*1024) && DisplayDev_UseUpdateFlags)
    {
      /* DMAable, must use func on write */
      FastMap_SetEntries(state,logadr,MEMC.PhysRam+(phys>>2),FastMap_LogRamFunc,flags|FASTMAP_W_FUNC,size);
    }
    else
    {
      /* Normal */
      FastMap_SetEntries(state,logadr,MEMC.PhysRam+(phys>>2),0,flags,size);
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
        if(MEMC.Vinit != RegVal)
        {
          MEMC.Vinit = RegVal;
          (DisplayDev_Current->DAGWrite)(state,RegNum,RegVal);
        }
        break;

      case 1: /* Vstart */
        if(MEMC.Vstart != RegVal)
        {
          MEMC.Vstart = RegVal;
          (DisplayDev_Current->DAGWrite)(state,RegNum,RegVal);
        }
        break;

      case 2: /* Vend */
        if(MEMC.Vend != RegVal)
        {
          MEMC.Vend = RegVal;
          (DisplayDev_Current->DAGWrite)(state,RegNum,RegVal);
        }
        break;

      case 3: /* Cinit */
        if(MEMC.Cinit != RegVal)
        {
          MEMC.Cinit = RegVal;
          (DisplayDev_Current->DAGWrite)(state,RegNum,RegVal);
        }
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
        ARMul_RebuildFastMap(state);
        break;
    }
}

static void MEMC_PutVal(ARMul_State *state, ARMword address)
{
    /* Logical-to-physical address translation */
    unsigned tmp;

    tmp = address - MEMORY_0x3800000_W_LOG2PHYS;

    address = ((address >> 4) & 0x100) | (address & 0xff);

    ARMul_PurgeFastMapPTIdx(state,address); /* Unmap old value */
    MEMC.PageTable[address] = tmp & 0x0fffffff;
    ARMul_RebuildFastMapPTIdx(state, address); /* Map in new value */
}

static ARMword FastMap_ROMMap1Func(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* Does nothing more than set ROMMapFlag to 2 and read a word of ROM */
  MEMC.ROMMapFlag = 2;
  ARMul_RebuildFastMap(state);
  return MEMC.ROMHigh[(addr & MEMC.ROMHighMask)>>2];
}

static ARMword FastMap_PhysRamFuncROMMap2(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  ARMword *phy;
  /* Read/write from phys RAM, switching to ROMMapFlag 0 if necessary */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap(state);
  }
  phy = FastMap_Log2Phy(FastMap_GetEntry(state,addr),addr&~3);
  if(!(flags & FASTMAP_ACCESSFUNC_WRITE))
    return *phy;
  if(flags & FASTMAP_ACCESSFUNC_BYTE)
  {
    ARMword shift = ((addr&3)<<3);
    phy = (ARMword*)(((FastMapUInt)phy)&~3);
    data = (data&0xff)<<shift;
    data |= (*phy) &~ (0xff<<shift);
  }
  *phy = data;
  *(FastMap_Phy2Func(state,phy)) = FASTMAP_CLOBBEREDFUNC;
  if(addr < 512*1024)
    FastMap_DMAAbleWrite(addr,data);
  return 0;
}

static ARMword FastMap_PhysRamFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  ARMword *phy, orig;
  /* Write to direct-mapped phys RAM. Address guaranteed to be valid. */
  addr -= MEMORY_0x2000000_RAM_PHYS;
  phy = &MEMC.PhysRam[addr>>2];
  orig = *phy;
  if(flags & FASTMAP_ACCESSFUNC_BYTE)
  {
    ARMword shift = ((addr&3)<<3);
    data = (data&0xff)<<shift;
    data |= orig &~ (0xff<<shift);
  }
  if(orig != data)
  {
    *phy = data;
    *(FastMap_Phy2Func(state,phy)) = FASTMAP_CLOBBEREDFUNC;
    /* Update DMA flags */
    FastMap_DMAAbleWrite(addr,data);
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
    ARMul_RebuildFastMap(state);
  }
  if(flags & FASTMAP_ACCESSFUNC_WRITE)
  {
    PutValIO(state,addr,data,flags&FASTMAP_ACCESSFUNC_BYTE);
    return 0;
  }
  return GetWord_IO(state,addr);
}

static ARMword FastMap_VIDCFunc(ARMul_State *state, ARMword addr,ARMword data,ARMword flags)
{
  /* May be a ROMMapFlag read */
  if((flags & (FASTMAP_ACCESSFUNC_WRITE|FASTMAP_ACCESSFUNC_STATECHANGE)) == FASTMAP_ACCESSFUNC_WRITE)
    return 0; /* Write without state change is bad! */
  if(MEMC.ROMMapFlag == 2)
  {
    MEMC.ROMMapFlag = 0;
    ARMul_RebuildFastMap(state);
  }
  if(flags & FASTMAP_ACCESSFUNC_WRITE)
  {
    (DisplayDev_Current->VIDCPutVal)(state,addr,data,flags&FASTMAP_ACCESSFUNC_BYTE);
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
    ARMul_RebuildFastMap(state);
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
    ARMul_RebuildFastMap(state);
  }
  if(flags & FASTMAP_ACCESSFUNC_WRITE)
  {
    MEMC_PutVal(state, addr);
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

void ARMul_RebuildFastMap(ARMul_State *state)
{
  ARMword i;
  FastMapEntry *entry;
  
  /* completely rebuild the fast map */
  switch(MEMC.ROMMapFlag)
  {
  case 0:
    {
      /* Map in logical RAM using the page tables */
      FastMap_SetEntries(state,0,0,0,0,0x2000000);

      for(i=0;i<512;i++)
        ARMul_RebuildFastMapPTIdx(state, i);
    }
    break;
  case 1:
    /* Map ROM to 0x0, using access func, even though we know the very first thing the processor will do is fetch from 0x0 and transition us away... */
    FastMap_SetEntries_Repeat(state,0,0,FastMap_ROMMap1Func,FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC|FASTMAP_R_FUNC,MEMC.ROMHighSize,0x800000);
    FastMap_SetEntries(state,0x800000,0,0,0,0x1800000);
    break;
  case 2:
    /* Map ROM to 0x0 */
    FastMap_SetEntries_Repeat(state,0,MEMC.ROMHigh,0,FASTMAP_R_USR|FASTMAP_R_OS|FASTMAP_R_SVC,MEMC.ROMHighSize,0x800000);
    FastMap_SetEntries(state,0x800000,0,0,0,0x1800000);
    break;
  }

  /* Map physical RAM */
  if(MEMC.ROMMapFlag == 2)
  {
    /* Use access func for all of it */
    FastMap_SetEntries(state,MEMORY_0x2000000_RAM_PHYS,0,FastMap_PhysRamFuncROMMap2,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_R_FUNC|FASTMAP_W_FUNC,16*1024*1024);
  }
  else
  {
    for(i=0;i<16*1024*1024;i+=4096)
    {
      ARMword phy = ARMul_ManglePhysAddr(i);
      if((phy < 512*1024) && DisplayDev_UseUpdateFlags)
      {
        /* Lower 512K must use access func for write
           But we can use a fast function (for when the OS has correctly detected our RAM setup) or a slow one. */
        if(i == phy)
        {
          /* Direct mapping, use fast func */
          FastMap_SetEntries(state,MEMORY_0x2000000_RAM_PHYS+i,MEMC.PhysRam+(phy>>2),FastMap_PhysRamFunc,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_W_FUNC,4096);
        }
        else
        {
          /* Indirect mapping, reuse LogRamFunc */
          FastMap_SetEntries(state,MEMORY_0x2000000_RAM_PHYS+i,MEMC.PhysRam+(phy>>2),FastMap_LogRamFunc,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_W_FUNC,4096);
        }
      }
      else
      {
        /* Remainder can use direct access for read/write */
        FastMap_SetEntries(state,MEMORY_0x2000000_RAM_PHYS+i,MEMC.PhysRam+(phy>>2),0,FASTMAP_R_SVC|FASTMAP_W_SVC,4096);
      }
    }
  }

  /* I/O space */
  FastMap_SetEntries(state,MEMORY_0x3000000_CON_IO,0,FastMap_ConIOFunc,FASTMAP_R_SVC|FASTMAP_W_SVC|FASTMAP_R_FUNC|FASTMAP_W_FUNC,0x400000);

  /* ROM Low/VIDC/DMA */
  /* Make life easier for ourselves by mapping in everything as DMA then overwriting with VIDC */
  if(MEMC.ROMLow && (MEMC.ROMMapFlag != 2))
    FastMap_SetEntries_Repeat(state,MEMORY_0x3400000_R_ROM_LOW,MEMC.ROMLow,FastMap_DMAFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC,MEMC.ROMLowSize,0x400000);
  else
    FastMap_SetEntries(state,MEMORY_0x3400000_R_ROM_LOW,0,FastMap_DMAFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC|FASTMAP_R_FUNC,0x400000);

  /* Overwrite with VIDC */
  entry = FastMap_GetEntryNoWrap(state,MEMORY_0x3400000_W_VIDEOCON);
  for(i=0;i<MEMORY_0x3600000_W_DMA_GEN-MEMORY_0x3400000_W_VIDEOCON;i+=4096)
  {
    entry->AccessFunc = FastMap_VIDCFunc;
    entry++;
  }

  /* ROM High/MEMC */
  if(MEMC.ROMHigh && (MEMC.ROMMapFlag != 2))
    FastMap_SetEntries_Repeat(state,MEMORY_0x3800000_R_ROM_HIGH,MEMC.ROMHigh,FastMap_MEMCFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC,MEMC.ROMHighSize,0x800000);
  else
    FastMap_SetEntries(state,MEMORY_0x3800000_R_ROM_HIGH,0,FastMap_MEMCFunc,FASTMAP_R_USR|FASTMAP_R_SVC|FASTMAP_R_OS|FASTMAP_W_SVC|FASTMAP_W_FUNC|FASTMAP_R_FUNC,0x800000);
}

#ifndef FASTMAP_INLINE
#include "arch/fastmap.c"
#endif
