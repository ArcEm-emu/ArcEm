/* (c) David Alan Gilbert 1995-1998 - see Readme file for copying info */

/*#define DEBUG_MEMC */
/*
    Used to be based on ArmVirt.c

    Modified by Dave Gilbert (arcem@treblig.org) to emulate an Archimedes
*/

#include <signal.h>
#include <stdio.h>
//#include <unistd.h>

#include "../armopts.h"
#include "../armdefs.h"
#include "../armrdi.h"

#include "armarc.h"
#include "archio.h"
#include "DispKbd.h"
#include "archio.h"
#include "fdc1772.h"
#include "ReadConfig.h"
#include "Version.h"

#ifdef DISASSEMBLY
#include "../../disassembler/Disass.h"
#include "../../disassembler/SWIList.h"
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

ARMword GetWord(ARMword address);

static void PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw,int Statechange, ARMEmuFunc newfunc);

struct MEMCStruct memc;

/*-----------------------------------------------------------------------------*/
static void PutWord(ARMul_State *state,ARMword address, ARMword data) {
  PutVal(state,address, data,0,1,ARMul_Emulate_DecodeInstr);
}; /* PutWord */

#define PRIVELIGED (statestr.NtransSig)

#define OSMODE(state) ((MEMC.ControlReg>>12)&1)

static ARMul_State *sigavailablestate=NULL;
static int Trace=0;
/*static unsigned int traceEnableAddrFromEnv;
#define ANALYTRACE
#define TRACEFILE "/discs/raida/trace"
static FILE *TraceFile;*/

/*------------------------------------------------------------------------------*/
void EnableTrace(void) {
  Trace=1;
};

/*------------------------------------------------------------------------------*/
/* OK - this is getting treeted as an odds/sodds engine - just hook up anything
   you need to do occasionally! */
static void DumpHandler(int sig) {
  ARMul_State *state=sigavailablestate;
  FILE *res;

  exit(0);

  EnableTrace();
  return;

  fprintf(stderr,"SIGUSR2 at PC=0x%x R[15]=0x%x\n",(unsigned int)statestr.pc,
                                            (unsigned int)statestr.Reg[15]);
#ifndef WIN32
  signal(SIGUSR2,DumpHandler);
#endif

  FDC_ReOpen(state,0);
  return;

  Trace=1;
  return;

  exit(0);
  res=fopen("memdump","wb");

  if (res==NULL) {
    fprintf(stderr,"Could not open memory dump file\n");
    return;
  };

  fwrite(MEMC.PhysRam,1,/*MEMC.RAMSize*/ 512*1024,res);

  fclose(res);
}; /* DumpHandler */

/*------------------------------------------------------------------------------*/
/* Uses MEMC.TmpPage which must have been previously been set up by a call to   */
/* checkabort                                                                   */
ARMword GetPhysAddress(unsigned int address) {
  ARMword pagetabval=MEMC.TmpPage;
  int PhysPage;
  ARMword result;

  if (MEMC.TmpPage==-1) return(0);

  switch (MEMC.PageSizeFlags) {
    case 0:
      /* 4K */
      PhysPage=pagetabval & 127;
      result=(address & 0xfff) | (PhysPage<<12);
      return(result);

    case 1:
      /* 8K */
      PhysPage=((pagetabval >> 1) & 0x3f) | ((pagetabval & 1) << 6);
      result=(address & 0x1fff) | (PhysPage<<13);
      return(result);

    case 2:
      /* 16K */
      PhysPage=((pagetabval >> 2) & 0x1f) | ((pagetabval & 3) << 5);
      result=(address & 0x3fff) | (PhysPage<<14);
      return(result);

    case 3:
      /* 32K */
      PhysPage=((pagetabval >> 3) & 0xf) | ((pagetabval & 1) << 4) | ((pagetabval & 2) << 5) | ((pagetabval & 4) << 3);
      result=(address & 0x7fff) | (PhysPage<<15);
      return(result);
  }; /* Page size switch */

  abort();
}; /* GetPhysAddress */


/*------------------------------------------------------------------------------*/
/* Look up an address in the pagetable and return the raw entry                 */
/* Return 1 if we find it                                                       */
int FindPageTableEntry_Search(unsigned int address, ARMword *PageTabVal) {
  int Current;
  unsigned int matchaddr;
  unsigned int mask;

  switch (MEMC.PageSizeFlags) {
    case 0: /* 4K */
      matchaddr = ((address & 0x7ff) << 12) |
                  ((address & 0x1800) >> 1);
      mask = 0x807ffc00;
      break;

    case 1: /* 8K */
      matchaddr = ((address & 0x3ff) << 13) |
                   (address & 0xc00);
      mask = 0x807fec00;
      break;

    case 2: /* 16K */
      matchaddr = ((address & 0x1ff) << 14) |
                  ((address & 0x600) << 1);
      mask = 0x807fcc00;
      break;

    case 3: /* 32K */
      matchaddr = ((address & 0xff) << 15) |
                  ((address & 0x300) << 2);
      mask = 0x807f8c00;
      break;

    default:
      abort();
  };

  for (Current = 0; Current < 128; Current++) {
    if ((MEMC.PageTable[Current] & mask)==matchaddr) {
      MEMC.OldAddress1 = address;
      MEMC.OldPage1 = MEMC.PageTable[Current];
      *PageTabVal   = MEMC.PageTable[Current];
      return(1);
    };
  };

  return(0);
}; /* FindPageTableEntry_Search */


static int FindPageTableEntry(unsigned int address, ARMword *PageTabVal) {
  address >>= (12 + MEMC.PageSizeFlags);
  if (address == MEMC.OldAddress1) {
    *PageTabVal = MEMC.OldPage1;
    return(1);
  };

  return FindPageTableEntry_Search(address, PageTabVal);
}


/*------------------------------------------------------------------------------*/
/* Return 0 if we should abort on this address */
static int CheckAbortR(int address) {
  ARMword PageTabVal;

  /*printf("CheckAbort for address=0x%x\n",address);*/

  /* First check if we are doing ROM remapping and are accessing a remapped location */
  /* If it is then map it into the ROM space */
  if ((MEMC.ROMMapFlag) && (address<0XC00000))
    address = 0x3400000 + address;

  switch ((address>>24)&3) {
    case 3:
      /* If we are in ROM and trying to read then there is no hastle */
      if (address>=0x3400000) {
        /* Its ROM read - no hastle */
        return(1);
      };
      /* NOTE: No break, IOC has same permissions as physical RAM */

    case 2:
      /* Priveliged only */
      if (PRIVELIGED)
        return(1);
      else
        return(0);

    case 0:
    case 1:
      /* OK - it's in logically mapped RAM */
      if (!FindPageTableEntry(address,&PageTabVal)) {
        MEMC.TmpPage=-1;
#ifdef DEBUG
        printf("ChecKAbortR at 0x%08x due to unmapped page\n",address);
#endif
        return(0);
      };

      MEMC.TmpPage=PageTabVal;

      /* The page exists - so if it's supervisor then it's OK */
      if (PRIVELIGED) return(1);

      if (MEMC.ControlReg & (1<<12)) {
        /* OS mode */
        /* it can read anything */
        return(1);
      } else {
        /* User mode */
        /* Extract page protection level */
        if (PageTabVal & 512) {
          MEMC.TmpPage=-1;
#ifdef DEBUG
          printf("CheckAbortR at 0x%08x due to bad user mode PPL PageTabVal=0x%x\n",address,PageTabVal);
#endif
          return(0);
        } else
          return 1;
      }; /* User mode */
  };
  return 0;
}; /* CheckAbortR */


/*------------------------------------------------------------------------------*/
/* Return 0 if we should abort on this address */
static int CheckAbortW(int address) {
  ARMword PageTabVal;

  /*printf("CheckAbort for address=0x%x\n",address);*/

  /* If we are in ROM and trying to read then there is no hastle */
  if (address>=0x3400000) {
    /* Write - only allowed in priveliged mode */
    /*fprintf(stderr,"Ntrans=%d\n",statestr.NtransSig); */
    if (PRIVELIGED) return(1); else {
      return(0);
    };
  };

  if (address>=0x2000000) {
    /* Priveliged only */
    if (PRIVELIGED) return(1); else {
      return(0);
    };
  };

  /* OK - its in logically mapped RAM */
  if (!FindPageTableEntry(address,&PageTabVal)) {
    MEMC.TmpPage=-1;
#ifdef DEBUG
    printf("Failed CheckAbortW at address0x%08x due to inability to find page\n",address);
#endif
    return(0);
  };

  MEMC.TmpPage=PageTabVal;

  /* The page exists - so if its superviser then its OK */
  if (PRIVELIGED) return(1);

  /* Extract page protection level */
  PageTabVal>>=8;
  PageTabVal&=3;
  if (MEMC.ControlReg & (1<<12)) {
    /* OS mode */

    /* it can only write if the top bit of the PPL is low */
    if (PageTabVal & 2) {
      MEMC.TmpPage=-1;
      return(0);
    } else {
     return(1);
    };
  } else {
    /* User mode */
    switch (PageTabVal) {
      case 0:
        return(1);

      case 1:
        MEMC.TmpPage=-1;
        return(0);

      case 2:
      case 3:
        MEMC.TmpPage=-1;
        return(0);
    };
  }; /* User mode */

  /* Hey - it shouldn't get here */
  abort();
  return(0);
}; /* CheckAbort */

/***************************************************************************\
*                      Initialise the memory interface                      *
\***************************************************************************/

unsigned ARMul_MemoryInit(ARMul_State *state, unsigned long initmemsize)
{
 PrivateDataType *PrivDPtr;
 FILE *ROMFile;
 unsigned int ROMWordNum,ROMWord;
 int PresPage;
 unsigned int index;

 PrivDPtr = (PrivateDataType *)malloc(sizeof(PrivateDataType));
 if (PrivDPtr == NULL) {
   fprintf(stderr,"ARMul_MemoryInit: malloc of PrivateDataType failed\n");
   exit(3);
 };

 statestr.MemDataPtr = (unsigned char *)PrivDPtr;

#ifdef DEBUG
 fprintf(stderr,"Reading config file....\n");
#endif
 ReadConfigFile(state);

 if (initmemsize) {
   statestr.MemSize = initmemsize;
 } else {
   statestr.MemSize = initmemsize = 4 * 1024 * 1024; /* seems like a good value! */
 };

 sigavailablestate = state;
#ifndef WIN32
 signal(SIGUSR2,DumpHandler);
#endif


 MEMC.RAMSize = initmemsize;
 /* and now the RAM */
 /* Was malloc - but I want it 0 init */
 MEMC.PhysRam = (ARMword *)calloc(MEMC.RAMSize,1);
 MEMC.PhysRamfuncs = (ARMEmuFunc*)malloc(((MEMC.RAMSize) / 4) * sizeof(ARMEmuFunc));
 if ((MEMC.PhysRam == NULL) || (MEMC.PhysRamfuncs == NULL)) {
  fprintf(stderr,"Couldn't allocate RAM space\n");
  exit(3);
 };

 for (index = 0; index < (MEMC.RAMSize / 4); index++)
   MEMC.PhysRamfuncs[index]=ARMul_Emulate_DecodeInstr;

 MEMC.ROMMapFlag=1; /* Map ROM to address 0 */
 MEMC.ControlReg=0; /* Defaults */
 MEMC.PageSizeFlags=0;

 /* Top bit set means it isn't valid */
 for (PresPage = 0; PresPage <128; PresPage++)
   MEMC.PageTable[PresPage]=0xffffffff;

#ifdef DEBUG
 fprintf(stderr," Loading ROM....\n ");
#endif


#ifdef __riscos__
 if (ROMFile = fopen("<ArcEm$Dir>.^.ROM", "rb"), ROMFile == NULL) {
   exit(2);
 }
#else
 if (ROMFile = fopen("ROM", "rb"), ROMFile == NULL) {
   fprintf(stderr, "Couldn't open ROM file\n");
   exit(1);
 };
#endif

 /* Find the rom file size */
 fseek(ROMFile,0l,SEEK_END);

 MEMC.ROMSize = (unsigned int)ftell(ROMFile);

 fseek(ROMFile, 0l, SEEK_SET);

 /* Allocate the ROM space */
 MEMC.ROM=(ARMword *)malloc(MEMC.ROMSize + 128 /* 128 is to try and ensure gap with next malloc to stop cache thrashing */);
 MEMC.Romfuncs=(ARMEmuFunc*)malloc((MEMC.ROMSize/4)*sizeof(ARMEmuFunc));
 if ((MEMC.ROM==NULL) || (MEMC.Romfuncs==NULL)) {
  fprintf(stderr,"Couldn't allocate space for ROM\n");
  exit(3);
 };

 for (ROMWordNum = 0; ROMWordNum < MEMC.ROMSize / 4; ROMWordNum++) {
#ifdef DEBUG
   if (!(ROMWordNum & 0x1ff)) {
     fprintf(stderr, ".");
     fflush(stderr);
   };
#endif

   ROMWord  =  fgetc(ROMFile);
   ROMWord |= (fgetc(ROMFile) << 8);
   ROMWord |= (fgetc(ROMFile) << 16);
   ROMWord |= (fgetc(ROMFile) << 24);
   MEMC.ROM[ROMWordNum] = ROMWord;
   MEMC.Romfuncs[ROMWordNum] = ARMul_Emulate_DecodeInstr;
 };

#ifdef DEBUG
 fprintf(stderr," ..Done\n ");
#endif

 DisplayKbd_Init(state);

 IO_Init(state);

 PRIVD->irqflags = 0;
 PRIVD->fiqflags = 0;

 for(index = 0;index < (512 * 1024) / UPDATEBLOCKSIZE; index++) {
   MEMC.UpdateFlags[index] = 1;
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

/***************************************************************************\
*                         Remove the memory interface                       *
\***************************************************************************/

void ARMul_MemoryExit(ARMul_State *state)
{
 free(MEMC.PhysRam);
 free(MEMC.ROM);
 free(PRIVD);
 return;
 }

/***************************************************************************\
*                        Load Word, Sequential Cycle                        *
\***************************************************************************/

ARMword ARMul_LoadWordS(ARMul_State *state,ARMword address)
{statestr.Numcycles++;

  address&=0x3ffffff;

 if (CheckAbortR(address)) {
  ARMul_CLEARABORT;
 } else {
#ifdef DEBUG
  printf("ARMul_LoadWordS abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",address,statestr.pc,statestr.Reg[15],PRIVELIGED,OSMODE(state),statestr.Mode);
#endif
  ARMul_DATAABORT(address);
  return(ARMul_ABORTWORD);
 };

 return(GetWord(address));
}

/***************************************************************************\
*                      Load Word, Non Sequential Cycle                      *
\***************************************************************************/

ARMword ARMul_LoadWordN(ARMul_State *state,ARMword address)
{statestr.Numcycles++;
  address&=0x3ffffff;

 if (CheckAbortR(address)) {
  ARMul_CLEARABORT;
 } else {
#ifdef DEBUG
  printf("ARMul_LoadWordN abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",address,statestr.pc,statestr.Reg[15],PRIVELIGED,OSMODE(state),statestr.Mode);
#endif
  ARMul_DATAABORT(address);
  return(ARMul_ABORTWORD);
 };

 return(GetWord(address));
}

/***************************************************************************\
*                     Load Byte, (Non Sequential Cycle)                     *
\***************************************************************************/

ARMword ARMul_LoadByte(ARMul_State *state,ARMword address) {
 unsigned int temp;
 int bytenum=address &3;

 address&=0x3ffffff;

 statestr.Numcycles++;

 if (CheckAbortR(address)) {
  ARMul_CLEARABORT;
 } else {
#ifdef DEBUG
  printf("ARMul_LoadByte abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",address,statestr.pc,statestr.Reg[15],PRIVELIGED,OSMODE(state),statestr.Mode);
#endif
  ARMul_DATAABORT(address);
  return(ARMul_ABORTWORD);
 };

 temp=GetWord(address);

 /* WARNING! Little endian only */
 temp>>=(bytenum*8);

 return(temp & 0xff);
}

/***************************************************************************\
*                       Store Word, Sequential Cycle                        *
\***************************************************************************/

void ARMul_StoreWordS(ARMul_State *state,ARMword address, ARMword data)
{statestr.Numcycles++;
 address&=0x3ffffff;

 if (CheckAbortW(address)) {
  ARMul_CLEARABORT;
 } else {
#ifdef DEBUG
  printf("ARMul_StoreWordS abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",address,statestr.pc,statestr.Reg[15],PRIVELIGED,OSMODE(state),statestr.Mode);
#endif
  ARMul_DATAABORT(address);
  return;
 };

 PutWord(state,address,data);
}

/***************************************************************************\
*                       Store Word, Non Sequential Cycle                        *
\***************************************************************************/

void ARMul_StoreWordN(ARMul_State *state, ARMword address, ARMword data)
{statestr.Numcycles++;
 address&=0x3ffffff;

 if (CheckAbortW(address)) {
  ARMul_CLEARABORT;
 } else {
#ifdef DEBUG
  printf("ARMul_StoreWordN abort on address 0x%08x PC=0x%x R[15]=0x%x priv=%d osmode=%d mode=%d\n",address,statestr.pc,statestr.Reg[15],PRIVELIGED,OSMODE(state),statestr.Mode);
#endif
  ARMul_DATAABORT(address);
  return;
 };

 PutWord(state,address,data);
}

/***************************************************************************\
*                    Store Byte, (Non Sequential Cycle)                     *
\***************************************************************************/

void ARMul_StoreByte(ARMul_State *state, ARMword address, ARMword data)
{
 address&=0x3ffffff;

 statestr.Numcycles++;


 if (CheckAbortW(address)) {
  ARMul_CLEARABORT;
 } else {
  ARMul_DATAABORT(address);
  return;
 };

 PutVal(state,address,data,1,1,ARMul_Emulate_DecodeInstr);
}

/***************************************************************************\
*                   Swap Word, (Two Non Sequential Cycles)                  *
\***************************************************************************/

ARMword ARMul_SwapWord(ARMul_State *state, ARMword address, ARMword data)
{ARMword temp;

 address&=0x3ffffff;
 statestr.Numcycles++;

 if (CheckAbortW(address)) {
  ARMul_CLEARABORT;
 } else {
  ARMul_DATAABORT(address);
  return(ARMul_ABORTWORD);
 };

 temp = GetWord(address);
 statestr.Numcycles++;
 PutWord(state,address,data);
 return(temp);
 }

/***************************************************************************\
*                   Swap Byte, (Two Non Sequential Cycles)                  *
\***************************************************************************/

ARMword ARMul_SwapByte(ARMul_State *state, ARMword address, ARMword data)
{ARMword temp;

 address&=0x3ffffff;
 if (CheckAbortW(address)) {
  ARMul_CLEARABORT;
 } else {
  ARMul_DATAABORT(address);
  return(ARMul_ABORTWORD);
 };

 temp = ARMul_LoadByte(state,address);
 ARMul_StoreByte(state,address,data);
 return(temp);
 }

/*-----------------------------------------------------------------------------*/
/* The caller MUST perform a full abort check first - if only to ensure that   */
/* a page in logical RAM does not abort.                                       */
ARMword GetWord(ARMword address) {
  /*printf("GetWord for address=0x%x\n",address);*/
  /* First check if we are doing ROM remapping and are accessing a remapped location */
  /* If it is then map it into the ROM space */
  if ((MEMC.ROMMapFlag) && (address<0XC00000)) {
    address=0x3400000+address;
    MEMC.ROMMapFlag=2;
    if (address>=0x3800000) address-=0x3800000; else address-=0x3400000;

    /* A simple ROM wrap around */
    while (address>MEMC.ROMSize)
      address-=MEMC.ROMSize;

    return(MEMC.ROM[address/4]);
  };

  switch ((address >> 24) &3) {
    case 3:
      if (address>=0x3400000) {
        if (address>=0x3800000) address-=0x3800000; else address-=0x3400000;

        if ((MEMC.ROMMapFlag==2)) {
          MEMC.OldAddress1=-1;
          MEMC.OldPage1=-1;
          MEMC.ROMMapFlag=0;
        };

        /* A simple ROM wrap around */
        while (address>MEMC.ROMSize)
          address-=MEMC.ROMSize;

        return(MEMC.ROM[address/4]);
      };

      if ((MEMC.ROMMapFlag==2)) {
        MEMC.ROMMapFlag=0;
        MEMC.OldAddress1=-1;
        MEMC.OldPage1=-1;
      };
      return(GetWord_IO(emu_state,address));

    case 2:
      if ((MEMC.ROMMapFlag==2)) {
        MEMC.ROMMapFlag=0;
        MEMC.OldAddress1=-1;
        MEMC.OldPage1=-1;
      };
      address-=0x2000000;

      /* I used to think memory wrapped after the real RAM - but it doesn't */
      /* appear to */
      if  (address >= MEMC.RAMSize) {
#ifdef DEBUG_MEMC
        fprintf(stderr,"GetWord returning dead0bad - after PhysRam\n");
#endif
        return(0xdead0bad);
        /*while (address>MEMC.RAMSize)
          address-=MEMC.RAMSize; */
      };

      return(MEMC.PhysRam[address/4]);

    case 0:
    case 1:
      /* Logically mapped RAM */
      address=GetPhysAddress(address);

      /* I used to think memory wrapped after the real RAM - but it doesn't */
      /* appear to */
      if (address >= MEMC.RAMSize) {
#ifdef DEBUG_MEMC
        fprintf(stderr,"GetWord returning dead0bad - after PhysRam (from log ram)\n");
#endif
        return(0xdead0bad);
        /*while (address>MEMC.RAMSize)
          address-=MEMC.RAMSize; */
      };

      return(MEMC.PhysRam[address/4]);
  };
  return 0;
}; /* GetWord */

/*-----------------------------------------------------------------------------*/
/* The caller MUST check for aborts before calling us!                         */
static void PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw,int Statechange, ARMEmuFunc newfunc) {
  if (address>=0x3800000) {
    /* Logical-to-physical address translation */
    int tmp;

    if (!Statechange) return;

    MEMC.OldAddress1=-1;
    MEMC.OldPage1=-1;
    if (MEMC.ROMMapFlag==2) MEMC.ROMMapFlag=0;
    tmp=address-0x3800000;
#ifdef DEBUG_MEMC
    {
      unsigned int logbaseaddr;
      unsigned int Entry;

      logbaseaddr=(tmp >>15) & 255;
      logbaseaddr|=(tmp >>2) & (256|512);
      logbaseaddr<<=(12+MEMC.PageSizeFlags); /* Actually all that masking is only OK for 32K */
      Entry=(tmp >>3)&0xf;
      Entry|=(tmp<<4)&16;
      Entry|=(tmp<<3)&32;
      Entry|=(tmp<<5)&64;
      fprintf(stderr,"PutVal: Pagetable write, address=0x%x entry=%d val=0x%x (logbaseaddress=0x%08x ppn=%d - for 32K size)\n",
              address,address & 127, tmp & 0x0fffffff,logbaseaddr,Entry);
    };
#endif
    MEMC.PageTable[address & 127]=tmp & 0x0fffffff;

    return;
  };

  if (address>=0x3600000) {
    /* DMA address generation - MEMC Control reg */
    ARMword RegNum,RegVal;

    if (!Statechange) return;

    if (MEMC.ROMMapFlag==2) MEMC.ROMMapFlag=0;

    RegNum=(address>>17) & 7;
    RegVal=(address>>2) & 0x7fff;

#ifdef DEBUG_MEMC
    fprintf(stderr,"Write to MEMC register: Reg=%d Val=0x%x\n",RegNum,RegVal);
#endif

    switch (RegNum) {
      case 0: /* Vinit */
        VideoRelUpdateAndForce(DISPLAYCONTROL.MustRedraw,MEMC.Vinit,RegVal);
        break;

      case 1: /* Vstart */
        VideoRelUpdateAndForce(DISPLAYCONTROL.MustRedraw,MEMC.Vstart,RegVal);
        break;

      case 2: /* Vend */
        VideoRelUpdateAndForce(DISPLAYCONTROL.MustRedraw,MEMC.Vend,RegVal);
        break;

      case 3: /* Cinit */
        MEMC.Cinit=RegVal;
        break;

      case 4: /* Sstart */
        MEMC.Sstart=RegVal;
        ioc.IRQStatus &= ~0x20; /* Take sound interrupt off */
        IO_UpdateNirq();
#ifdef DEBUG_MEMC
        fprintf(stderr,"Write to MEMC Sstart register\n");
#endif
        break;

      case 5: /* SendN */
        MEMC.SendN = RegVal;
#ifdef DEBUG_MEMC
        fprintf(stderr,"Write to MEMC Send register\n");
#endif
        break;

      case 6: /* Sptr */
#ifdef DEBUG_MEMC
        fprintf(stderr,"Write to MEMC Sptr register\n");
#endif
        MEMC.Sptr = RegVal; /* NO. Should just cause a sound buffer swap? */
        break;

      case 7: /* MEMC Control register */
#ifdef DEBUG_MEMC
        fprintf(stderr,"MEMC Control register set to 0x%x by PC=0x%x R[15]=0x%x\n",address,(unsigned int)statestr.pc,(unsigned int)statestr.Reg[15]);
#endif
        MEMC.ControlReg=address;
        MEMC.PageSizeFlags = (MEMC.ControlReg & 12) >> 2;
        MEMC.OldAddress1 = -1;
        MEMC.OldPage1 = -1;
        break;
    };
    return;
  }; /* DMA address gen/MEMC control */

  if (address>=0x3400000) {
    if (!Statechange) return;

    if (MEMC.ROMMapFlag==2) MEMC.ROMMapFlag=0;
    /* VIDC */
    VIDC_PutVal(state,address,data,bNw);
    return;
  };

  if (address>=0x3000000) {
    if (!Statechange) return;

    if (MEMC.ROMMapFlag==2) MEMC.ROMMapFlag=0;
    /* IOC */
    PutValIO(state,address,data,bNw);
    return;
  };

  if (address<0x2000000) {
    /* Logically mapped RAM - shift the address so that the physical ram code will do it */
    address=GetPhysAddress(address);
  } else {
    /* Physical ram */
    if (MEMC.ROMMapFlag==2) MEMC.ROMMapFlag=0;
    address-=0x2000000;
  };

  if (address>=MEMC.RAMSize) return;

  /* Do updating for VIDC emulation */
  /* See if it's in DMAble ram */
  if (address<512*1024)
    MEMC.UpdateFlags[address/UPDATEBLOCKSIZE]++;

  if (bNw) {
    static unsigned int masktab[]={0xffffff00,0xffff00ff,0xff00ffff,0x00ffffff};
    ARMword mask;
    ARMword tmp;
    data&=0xff;

    mask=masktab[address & 3];
    tmp=MEMC.PhysRam[address/4] & mask;
    data=tmp | (data << ((address & 3)*8));
  };

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

  if (address < ROScreenExtent)
    ROScreenMemory[address/4] = data;
#endif

  MEMC.PhysRam[address/4]=data;
  MEMC.PhysRamfuncs[address/4]=newfunc;

  return;
} /* PutVal */
