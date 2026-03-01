
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

void *screenbmp, *cursorbmp;
size_t screenpitch, cursorpitch;

int rMouseX = 0;
int rMouseY = 0;
int rMouseWidth = 0;
int rMouseHeight = 0;

#define SDD_Name(x) sdd_Mac_##x

typedef uint32_t SDD_HostColour;
typedef SDD_HostColour *SDD_Row;

static void SDD_Name(RefreshMouse)(ARMul_State *state);

static void SDD_Name(Host_PollDisplay)(ARMul_State *state);

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,
                                      int hz);

static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,uint_fast16_t col)
{
  int r, g, b;

  UNUSED_VAR(state);

  /* Convert to 8-bit component values */
  r = (col & 0x00f);
  g = (col & 0x0f0);
  b = (col & 0xf00) >> 8;
  /* May want to tweak this a bit at some point? */
  r |= r<<4;
  g |= g>>4;
  b |= b<<4;
  return (b<<24) | (g<<16) | (r<<8) | 0xFF;
}

static void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,
                               unsigned int count)
{
  UNUSED_VAR(state);
  (*row) += count;
}

static SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  UNUSED_VAR(state);

  return ((SDD_Row)(void *)((uint8_t *)screenbmp + screenpitch*row))+offset;
}

const int SDD_RowsAtOnce = 1;

static void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row)
{
  /* nothing */
  UNUSED_VAR(state);
  UNUSED_VAR(row);
}

static void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,
                               SDD_HostColour col)
{
  UNUSED_VAR(state);
  *(*row)++ = col;
}

static void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,
                                SDD_HostColour col,unsigned int count)
{
  UNUSED_VAR(state);
  while(count--) {
    *(*row)++ = col;
  }
}

static void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,
                                    unsigned int count)
{
  /* nothing */
  UNUSED_VAR(state);
  UNUSED_VAR(row);
  UNUSED_VAR(count);
}

static void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row)
{
  /* nothing */
  UNUSED_VAR(state);
  UNUSED_VAR(row);
}

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,
                                      int hz)
{
  UNUSED_VAR(hz);

  HD.Width = width;
  HD.Height = height;
  HD.XScale = 1;
  HD.YScale = 1;
  dispatch_sync(dispatch_get_main_queue(), ^{
    resizeWindow(HD.Width,HD.Height);
  });
}

/* Refresh the mouse's image */
static void SDD_Name(RefreshMouse)(ARMul_State *state) {
  int x,y,xs,offset, pix, repeat;
  int memptr;
  int HorizPos;
  int Width = 32*HD.XScale;
  int Height = ((int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart)*HD.YScale;
  int VertPos;
  int diboffs;
  SDD_HostColour cursorPal[4];
  SDD_HostColour *host_dibbmp = (SDD_HostColour *)screenbmp;
  SDD_HostColour *host_curbmp = (SDD_HostColour *)cursorbmp;

  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  HorizPos = HorizPos*HD.XScale+HD.XOffset;
  VertPos = VertPos*HD.YScale+HD.YOffset;

  if (Height < 0) Height = 0;
  if (VertPos < 0) VertPos = 0;

  rMouseX = HorizPos;
  rMouseY = VertPos;
  rMouseWidth = Width;
  rMouseHeight = Height;

  /* Cursor palette */
  cursorPal[0] = SDD_Name(Host_GetColour)(state,0);
  for(x=0; x<3; x++) {
    cursorPal[x+1] = SDD_Name(Host_GetColour)(state,VIDC.CursorPalette[x]);
  }

  offset=0;
  memptr=MEMC.Cinit*16;
  repeat=0;
  host_dibbmp = (SDD_HostColour *)(void *) ((uint8_t *)host_dibbmp + (rMouseY*screenpitch));
  for(y=0;y<Height;y++) {
    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++) {
        pix = ((tmp[x/16]>>((x & 15)*2)) & 3);

        for(xs=0;xs<HD.XScale;xs++) {
          diboffs = rMouseX + (x*HD.XScale) + xs;

          host_curbmp[(x*HD.XScale) + xs] =
                  (pix || diboffs < 0 || diboffs >= HD.Width) ?
                  cursorPal[pix] : host_dibbmp[diboffs];
        } /* xs */
      } /* x */
    } else {
      return;
    }
    if (++repeat == HD.YScale) {
      memptr+=8;
      offset+=8;
      repeat = 0;
    }
    host_dibbmp = (SDD_HostColour *)(void *) ((uint8_t *)host_dibbmp + screenpitch);
    host_curbmp = (SDD_HostColour *)(void *) ((uint8_t *)host_curbmp + cursorpitch);
  } /* y */
}

static void SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  SDD_Name(RefreshMouse)(state);
  dispatch_sync(dispatch_get_main_queue(), ^{
    updateDisplay(0, 0, HD.Width, HD.Height);
  });
}

int
Kbd_PollHostKbd(ARMul_State *state)
{
  /* Keyboard and mouse input is handled in ArcemView */
  UNUSED_VAR(state);
  return 0;
}

bool
DisplayDev_Init(ARMul_State *state)
{
  return DisplayDev_Set(state,&SDD_DisplayDev);
}
