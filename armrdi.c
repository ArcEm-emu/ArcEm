/*  armrdi.c -- ARMulator RDI interface:  ARM6 Instruction Emulator.
    Copyright (C) 1994 Advanced RISC Machines Ltd.
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdarg.h>
#include <string.h>
//#include <ctype.h>

#include "arch/Version.h"
#include "armdefs.h"
#include "armemu.h"
#include "armos.h"
#include "armrdi.h"

#include "dbg_cp.h"
#include "dbg_conf.h"
#include "dbg_rdi.h"
#include "dbg_hif.h"

/***************************************************************************\
*                               Declarations                                *
\***************************************************************************/

#define Watch_AnyRead (RDIWatch_ByteRead+RDIWatch_HalfRead+RDIWatch_WordRead)
#define Watch_AnyWrite (RDIWatch_ByteWrite+RDIWatch_HalfWrite+RDIWatch_WordWrite)

/*static unsigned FPRegsAddr;*/ /* last known address of FPE regs */
#define FPESTART 0x2000L
#define FPEEND   0x8000L

#define IGNORE(d) (d = d)
#ifdef RDI_VERBOSE
#define TracePrint(s) \
 if (rdi_log & 1) ARMul_DebugPrint s
#else
#define TracePrint(s)
#endif

ARMul_State *emu_state = NULL;
static unsigned BreaksSet; /* The number of breakpoints set */

static int rdi_log = 0; /* debugging  ? */

#define LOWEST_RDI_LEVEL 0
#define HIGHEST_RDI_LEVEL 1
static int MYrdi_level = LOWEST_RDI_LEVEL;

typedef struct BreakNode BreakNode;
typedef struct WatchNode WatchNode;

struct BreakNode { /* A breakpoint list node */
   BreakNode *next;
   ARMword address; /* The address of this breakpoint */
   unsigned type; /* The type of comparison */
   ARMword bound; /* The other address for a range */
   ARMword inst;
   };

struct WatchNode { /* A watchpoint list node */
   WatchNode *next;
   ARMword address; /* The address of this watchpoint */
   unsigned type; /* The type of comparison */
   unsigned datatype; /* The type of access to watch for */
   ARMword bound; /* The other address for a range */
   };

BreakNode *BreakList = NULL;
WatchNode *WatchList = NULL;

#ifdef RDI_VERBOSE
static void ARMul_DebugPrint_i(const Dbg_HostosInterface *hostif, const char *format, ...)
{ va_list ap;
  va_start(ap, format);
  hostif->dbgprint(hostif->dbgarg, format, ap);
  va_end(ap);
}

static void ARMul_DebugPrint(ARMul_State *state, const char *format, ...)
{ va_list ap;
  va_start(ap, format);
  if(!(rdi_log & 8))
    state->hostif->dbgprint(state->hostif->dbgarg, format, ap);
  va_end(ap);
}
#endif

#define CONSOLE_PRINT_MAX_LEN 128

void ARMul_ConsolePrint(ARMul_State *state, const char *format, ...)
{
  va_list ap;
  int ch;
  char *str, buf[CONSOLE_PRINT_MAX_LEN];

  va_start(ap, format);
  vsprintf(buf, format, ap);

  str = buf;
  while ((ch = *str++) != 0) {
    state->hostif->writec(state->hostif->hostosarg, ch);
  }
}

#ifdef UNUSED__STOP_COMPILER_WARNINGS
static void ARMul_DebugPause(ARMul_State *state)
{
  if(!(rdi_log & 8))
  state->hostif->dbgpause(state->hostif->dbgarg);
}
#endif

/***************************************************************************\
*                                 RDI_open                                  *
\***************************************************************************/

static void InitFail(int exitcode, char const *which) {
  ARMul_ConsolePrint(emu_state, "%s interface failed to initialise. Exiting\n",
                            which);
  exit(exitcode);
}

static void RDIInit(unsigned type)
{if (type == 0) { /* cold start */
    BreaksSet = 0;
    }
 }

#define UNKNOWNPROC 0

typedef struct { char name[16]; unsigned val; } Processor;

Processor const p_arm2   = {"ARM2",    ARM2};
Processor const p_arm2as = {"ARM2AS",  ARM2as};
Processor const p_arm3   = {"ARM3",    ARM3};
Processor const p_unknown= {"",      UNKNOWNPROC};

Processor const *const processors[] = {
  &p_arm2,
  &p_arm2as,
  &p_arm3,
  &p_unknown
};

typedef struct ProcessorConfig ProcessorConfig;
struct ProcessorConfig {
  long id[2];
  ProcessorConfig const *self;
  long count;
  Processor const * const *processors;
};

ProcessorConfig const processorconfig = {
  { ((((((long)'x' << 8) | ' ') << 8) | 'c') << 8) | 'p',
    ((((((long)'u' << 8) | 's') << 8) | ' ') << 8) | 'x'
  },
  &processorconfig,
  16,
  processors
};

static int RDI_open(unsigned type, const Dbg_ConfigBlock *config,
                    const Dbg_HostosInterface *hostif,
                    struct Dbg_MCState *dbg_state)
/* Initialise everything */
{int virgin = (emu_state == NULL);
 IGNORE(dbg_state);

#ifdef RDI_VERBOSE
 if (rdi_log & 1) {
    if (virgin)
       ARMul_DebugPrint_i(hostif, "RDI_open: type = %d\n",type);
    else
       ARMul_DebugPrint(state, "RDI_open: type = %d\n",type);
 }
#endif

 if (type & 1) { /* Warm start */
    ARMul_Reset(emu_state);
    RDIInit(1);
    }
 else {
   if (virgin) {
     ARMul_EmulateInit();
     emu_state = ARMul_NewState();
     emu_state->hostif = hostif;
     {
       int req = config->processor;
       unsigned processor = processors[req]->val;
       ARMul_SelectProcessor(emu_state, processor);
       ARMul_Reset(emu_state);
       ARMul_ConsolePrint(emu_state, "%s, %s\n", Version, processors[req]->name);
     }
     if (ARMul_MemoryInit(emu_state,config->memorysize) == FALSE)
       InitFail(1, "Memory");
     if (ARMul_CoProInit(emu_state) == FALSE)
       InitFail(2, "Co-Processor");
     if (ARMul_OSInit(emu_state) == FALSE)
       InitFail(3, "Operating System");
   }
   ARMul_Reset(emu_state);
   RDIInit(0);
   }
  if (type & 2) { /* Reset the comms link */
    /* what comms link ? */
  }

  return( RDIError_LittleEndian);
}

/***************************************************************************\
*                                RDI_close                                  *
\***************************************************************************/

static int RDI_close(void)
{
 TracePrint((emu_state, "RDI_close\n"));
 ARMul_OSExit(emu_state);
 ARMul_CoProExit(emu_state);
 ARMul_MemoryExit(emu_state);
 return(RDIError_NoError);
 }

/***************************************************************************\
*                               RDI_CPUwrite                                *
\***************************************************************************/

static int RDI_CPUwrite(unsigned mode, unsigned long mask, ARMword const buffer[])
{int i, upto;


 TracePrint((state, "RDI_CPUwrite: mode=%.8x mask=%.8lx", mode, mask));
#ifdef RDI_VERBOSE
 if (rdi_log & 1) {
    for (upto = 0, i = 0; i <= 20; i++)
       if (mask & (1L << i)) {
          ARMul_DebugPrint(state, "%c%.8lx",upto%4==0?'\n':' ',buffer[upto]);
          upto++;
          }
    ARMul_DebugPrint(state, "\n");
    }
#endif

 if (mode == RDIMode_Curr)
    mode = (unsigned)(ARMul_GetCPSR(emu_state) & MODEBITS);

 for (upto = 0, i = 0; i < 15; i++)
    if (mask & (1L << i))
       ARMul_SetReg(emu_state,mode,i,buffer[upto++]);

 if (mask & RDIReg_R15)
    ARMul_SetR15(emu_state,buffer[upto++]);

 if (mask & RDIReg_PC) {

   ARMul_SetPC(emu_state,buffer[upto++]);
 }
 if (mask & RDIReg_CPSR)
    ARMul_SetCPSR(emu_state,buffer[upto++]);

 if (mask & RDIReg_SPSR)
    ARMul_SetSPSR(emu_state,mode,buffer[upto++]);

 return(RDIError_NoError);
}

/***************************************************************************\
*            Internal functions for breakpoint table manipulation           *
\***************************************************************************/

#ifdef UNUSED__STOP_COMPILER_WARNINGS
static void deletewatchnode(WatchNode **prevp)
{ WatchNode *p = *prevp;
  *prevp = p->next;
  free((char *)p);
}
#endif

#ifdef UNUSED__STOP_COMPILER_WARNINGS
static int removewatch(ARMword address, unsigned type)
{ WatchNode *p, **prevp = &WatchList;
  for (; (p = *prevp) != NULL; prevp = &p->next)
    if (p->address == address && p->type == type) { /* found a match */
       deletewatchnode(prevp);
       return TRUE;
    }
  return FALSE; /* never found a match */
}
#endif

#ifdef UNUSED__STOP_COMPILER_WARNINGS
static WatchNode *installwatch(ARMword address, unsigned type, unsigned datatype,
                               ARMword bound)
{ WatchNode *p = (WatchNode *)malloc(sizeof(WatchNode));
  p->next = WatchList;
  WatchList = p;
  p->address = address;
  p->type = type;
  p->datatype = datatype;
  p->bound = bound;
  return p;
}
#endif

/***************************************************************************\
*                               RDI_execute                                 *
\***************************************************************************/

static int RDI_execute(PointHandle *handle)
{
  TracePrint((emu_state, "RDI_execute\n"));

  ARMul_DoProg(emu_state);  

  emu_state->Reg[15] -= 8; /* undo the pipeline */
  return(0);
}

/***************************************************************************\
*                               RDI_info                                    *
\***************************************************************************/

static int RDI_info(unsigned type, ARMword *arg1, ARMword *arg2)
{
  switch (type) {
  case RDIInfo_Target:
    TracePrint((emu_state, "RDI_Info_Target\n"));
    /* Emulator, speed 10**5 IPS */
    *arg1 = 5 | HIGHEST_RDI_LEVEL << 5 | LOWEST_RDI_LEVEL << 8;
    *arg2 = 1298224434;
    return RDIError_NoError;

  case RDIInfo_Points:
  { ARMword n = RDIPointCapability_Comparison | RDIPointCapability_Range |
                RDIPointCapability_Mask | RDIPointCapability_Status;
    TracePrint((emu_state, "RDI_Info_Points\n"));
    *arg1 = n;
    return RDIError_NoError;
  }

  case RDIInfo_Step:
    TracePrint((emu_state, "RDI_Info_Step\n"));
    *arg1 =  RDIStep_Single;
    return RDIError_NoError;

  case RDIInfo_MMU:
    TracePrint((emu_state, "RDI_Info_MMU\n"));
    *arg1 = 1313820229;
    return RDIError_NoError;

  case RDIVector_Catch:
    TracePrint((emu_state, "RDIVector_Catch %.8lx\n", *arg1));
    emu_state->VectorCatch = (unsigned)*arg1;
    return RDIError_NoError;

  case RDISet_Cmdline:
    TracePrint((emu_state, "RDI_Set_Cmdline %s\n", (char *)arg1));
    emu_state->CommandLine = (char *)malloc((unsigned)strlen((char *)arg1)+1);
    (void)strcpy(emu_state->CommandLine,(char *)arg1);
    return RDIError_NoError;

  case RDIErrorP:
    *arg1 = ARMul_OSLastErrorP(emu_state);
    TracePrint((emu_state, "RDI_ErrorP returns %ld\n", *arg1));
    return RDIError_NoError;

  case RDIInfo_Log:
    *arg1 = (ARMword)rdi_log;
    return RDIError_NoError;

  case RDIInfo_SetLog:
    rdi_log = (int)*arg1;
    return RDIError_NoError;

  case RDIInfo_CoPro:
    return RDIError_NoError;

  case RDIPointStatus_Watch:
    { WatchNode *p, *handle = (WatchNode *)*arg1;
      for (p = WatchList; p != NULL; p = p->next)
        if (p == handle) {
          *arg1 = -1;
          *arg2 = 1;
          return RDIError_NoError;
        }
      return RDIError_NoSuchPoint;
    }

  case RDIPointStatus_Break:
    { BreakNode *p, *handle = (BreakNode *)*arg1;
      for (p = BreakList; p != NULL; p = p->next)
        if (p == handle) {
          *arg1 = -1;
          *arg2 = 1;
          return RDIError_NoError;
        }
      return RDIError_NoSuchPoint;
    }

  case RDISet_RDILevel:
    if (*arg1 < LOWEST_RDI_LEVEL || *arg1 > HIGHEST_RDI_LEVEL)
      return RDIError_IncompatibleRDILevels;
    MYrdi_level = *arg1;
    return RDIError_NoError;

  default:
    return RDIError_UnimplementedMessage;

  }
}

static RDI_NameList const *RDI_cpunames(void) {
  return (RDI_NameList const *)&processorconfig.count;
}

const struct RDIProcVec armul_rdi = {
    "ARMUL",
    RDI_open,
    RDI_close,
    NULL,
    NULL,
    NULL,
    RDI_CPUwrite,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    RDI_execute,
    NULL,
    RDI_info,

    0, /*pointinq*/
    0, /*addconfig*/
    0, /*loadconfigdata*/
    0, /*selectconfig*/
    0, /*drivernames*/

    RDI_cpunames
};

