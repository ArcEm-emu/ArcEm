
/*****************************************************************************
 *
 * Copyright (C) 2002 Michael Dales
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Name   : DispKbd.c
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 *
 ****************************************************************************/


#include <dispatch/dispatch.h>

#include "../armdefs.h"
#include "../arch/armarc.h"
#include "../arch/keyboard.h"
#include "../arch/displaydev.h"
#include "../arch/dbugsys.h"
#include "../eventq.h"
#include "win.h"
#include "../arch/ControlPane.h"

uint32_t *screenbmp;
uint32_t *cursorbmp;

int rMouseX = 0;
int rMouseY = 0;
int rMouseHeight = 0;

static void RefreshMouse(ARMul_State *state);

#define SDD_Name(x) sdd_Mac_##x

typedef uint32_t SDD_HostColour;
typedef SDD_HostColour *SDD_Row;

static void SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  RefreshMouse(state);
  dispatch_sync(dispatch_get_main_queue(), ^{
    updateDisplay(0, 0, MonitorWidth, MonitorHeight, 1);
  });
}

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,
                                      int hz);

static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,uint_fast16_t col)
{
  /* Convert to 8-bit component values */
  int r = (col & 0x00f);
  int g = (col & 0x0f0);
  int b = (col & 0xf00) >> 8;
  /* May want to tweak this a bit at some point? */
  r |= r<<4;
  g |= g>>4;
  b |= b<<4;
  return (b<<24) | (g<<16) | (r<<8) | 0xFF;
}

static void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,
                               unsigned int count)
{
  (*row) += count;
}

static SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  SDD_Row dibbmp = screenbmp;

  return &dibbmp[(row*MonitorWidth)+offset];
}

const int SDD_RowsAtOnce = 1;

static void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row)
{
  
}

static void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,
                               SDD_HostColour col)
{
  *(*row)++ = col;
}

static void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,
                                SDD_HostColour col,unsigned int count)
{
  while(count--) {
    *(*row)++ = col;
  }
}

static void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,
                                    unsigned int count)
{
  
}

static void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row)
{
  
}

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,
                                      int hz)
{
  HD.Width = width;
  HD.Height = height;
  HD.XScale = 1;
  HD.YScale = 1;
  dispatch_async(dispatch_get_main_queue(), ^{
    resizeWindow(HD.Width,HD.Height);
  });
}

static void RefreshMouse(ARMul_State *state)
{
  SDD_Row curbmp = cursorbmp;
  SDD_Row dibbmp = screenbmp;
  int offset, pix, repeat;
  int memptr;
  int HorizPos;
  int Height = ((int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart)*HD.YScale;
  int VertPos;
  int diboffs;
  SDD_HostColour cursorPal[4];
  
  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  HorizPos = HorizPos*HD.XScale+HD.XOffset;
  VertPos = VertPos*HD.YScale+HD.YOffset;
  
  if (Height < 0) Height = 0;
  if (Height > 32) Height = 32;
  if (VertPos < 0) VertPos = 0;
  
  rMouseX = HorizPos;
  rMouseY = VertPos;
  rMouseHeight = Height;
  
  /* Cursor palette */
  cursorPal[0] = 0;
  for(int x=0; x<3; x++) {
    cursorPal[x+1] = SDD_Name(Host_GetColour)(state,VIDC.CursorPalette[x]);
  }
  
  offset=0;
  memptr=MEMC.Cinit*16;
  repeat=0;
  for (int y=0; y < 32; y++) {
    if (offset < 512 * 1024 && y < Height) {
      ARMword tmp[2];
      
      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];
      
      for (int x=0; x < 32; x++) {
        pix = ((tmp[x/16]>>((x & 15)*2)) & 3);
        diboffs = rMouseX + x + (rMouseY + y) * MonitorWidth;
        
        curbmp[x + (y * 32)] =
        (pix || diboffs < 0 || diboffs > MonitorSize) ?
        cursorPal[pix] : dibbmp[diboffs];
      } /* x */
    } else if (rMouseY + y < MonitorHeight) {
      for (int x=0; x < 32; x++) {
        diboffs = rMouseX + x + (rMouseY + y) * MonitorWidth;

        curbmp[x + (y * 32)] =
        (diboffs < 0 || diboffs > MonitorSize) ?
        cursorPal[0] : dibbmp[diboffs];
      } /* x */
    } else {
      return;
    }
    if (++repeat == HD.YScale) {
      memptr+=8;
      offset+=8;
      repeat = 0;
    }
  } /* y */
}

int
Kbd_PollHostKbd(ARMul_State *state)
{
  /* Keyboard and mouse input is handled in ArcemView */
  return 0;
}

bool
DisplayDev_Init(ARMul_State *state)
{
  return DisplayDev_Set(state,&SDD_DisplayDev);
}
