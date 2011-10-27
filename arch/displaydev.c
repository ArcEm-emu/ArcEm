/*
  arch/displaydev.c

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Part of Arcem released under the GNU GPL, see file COPYING
  for details.

  Utility functions for bridging the gap between the host display code and the
  emulator core
*/
#include "armdefs.h"
#include "displaydev.h"
#include "archio.h"

const DisplayDev *DisplayDev_Current = NULL;

int DisplayDev_Set(ARMul_State *state,const DisplayDev *dev)
{
  struct Vidc_Regs Vidc;
  if(dev == DisplayDev_Current)
    return 0;
  if(DisplayDev_Current)
  {
    Vidc = VIDC;
    (DisplayDev_Current->Shutdown)(state);
    DisplayDev_Current = NULL;
  }
  else
  {
    memset(&Vidc,0,sizeof(Vidc));
  }
  if(dev)
  {
    int ret = (dev->Init)(state,&Vidc);
    if(ret)
      return ret;
    DisplayDev_Current = dev;
  }
  return 0;
}

void DisplayDev_GetCursorPos(ARMul_State *state,int *x,int *y)
{
  static const signed char offsets[4] = {19-6,11-6,7-6,5-6};
  *x = VIDC.Horiz_CursorStart-(VIDC.Horiz_DisplayStart*2+offsets[(VIDC.ControlReg & 0xc)>>2]);
  *y = VIDC.Vert_CursorStart-VIDC.Vert_DisplayStart;
}

static const unsigned long vidcclocks[4] = {24000000,25000000,36000000,24000000};

unsigned long DisplayDev_GetVIDCClockIn(void)
{
  return vidcclocks[ioc.IOEBControlReg & IOEB_CR_VIDC_MASK];
}
