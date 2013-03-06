/*
   arch/paldisplaydev.c

   (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>
   Based in part on original video code by David Alan Gilbert et al 

   Part of Arcem, covered under the GNU GPL, see file COPYING for details

   This is the core implementation of the palettised display driver. It's a
   driver for use with palettised output modes. Overall it's faster and uses
   less memory than the standard display driver, but it lacks the ability to
   cope with advanced things like mid-frame palette swaps. Border display
   method is the responsibility of the host, but the core will generate border
   update notifications. Cursor display is fully the responsibility of the host.

   The output buffer is updated once per frame, at the start of the vblank
   period.

   Where possible the code will try to simply copy the source pixels straight
   to the destination buffer. For situations where this isn't possible (e.g. the
   host doesn't support 1/2/4bpp palettised modes, or horizontal upscaling is
   desired) a lookup table is used to convert the data. This lookup table
   contains up to 256 entries and produces up to 32 bits of output per entry.

   E.g. 1bpp and 2bpp source data can be converted to 8bpp at a rate of four
   pixels per table lookup (since 8*4 = 32 bit output width). 4bpp data can be
   converted to 8bpp at a rate of two pixels per table lookup (since 4*2 = 8,
   the maximum number of input bits), or converted to 8bpp with 2x upscaling at
   the same rate.

   Unfortunately using a lookup table isn't always the best solution (e.g.
   simple 8bpp upscaling), so other algorithms may be added in the future.
*/

/*
   This file is intended to be #included directly by another source file in
   order to generate a working display driver.

   Before including the file, be sure to define the following
   types/symbols/macros:

   PDD_Name(x)
    - Macro used to convert symbol name 'x' into an instance-specific version
      of the symbol. e.g.
      "#define PDD_Name(x) MyPDD_##x"

   void PDD_Name(Host_PollDisplay)(ARMul_State *state)
    - Function that gets called after rendering each frame

   void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,
                                  int depth,int hz)
    - Function that gets called when the display mode needs changing.
    - 'Depth' is the log2 of the BPP
    - The implementation must change to the most appropriate display mode
      available and fill in the Width, Height, XScale and YScale members of the
      HostDisplay struct to reflect the new display parameters.
    - It must also fill in ExpandTable & ExpandFactor if BPP conversion or
      horizontal upscaling is required
    - Currently, failure to find a suitable mode isn't supported - however it
      shouldn't be too hard to keep the screen blanked (not ideal) by keeping
      DC.ModeChanged set to 1

   void PDD_Name(Host_SetPaletteEntry)(ARMul_State *state,int i,
                                       uint_fast16_t phys)
    - Function that gets called when a palette entry needs updating
    - 'phys' is a 13-bit VIDC physical colour

   void PDD_Name(Host_SetBorderColour)(ARMul_State *state,uint_fast16_t phys)
    - Function that gets called when the border colour needs updating
    - 'phys' is a 13-bit VIDC physical colour

   void PDD_Name(Host_DrawBorderRect)(ARMul_State *state,int x,int y,int width,
                                      int height)
    - Function that should fill an area of the display with the border colour

   PDD_Row
    - A data type that acts as an interator/accessor for a screen row.

   PDD_Row PDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset,
                                   int *alignment)
    - Function to return a PDD_Row instance suitable for accessing the indicated
      row, starting from the given X offset (in host pixels)
    - 'alignment' must be equivalent to the 'outoffset' value that will be
      returned by the first call to Host_BeginUpdate. I.e. the bit offset at
      which the first pixel will start at within the area being accessed.

   void PDD_Name(Host_EndRow)(ARMul_State *state,PDD_Row *row)
    - Function to end the use of a PDD_Row
    - Where a PDD_Row has been copied via pass-by-value, currently only the
      original instance will have Host_EndRow called on it.

   ARMword *PDD_Name(Host_BeginUpdate)(ARMul_State *state,PDD_Row *row,
                                       unsigned int count,int *outoffset)
    - Function called when the driver is about to write to 'count' bits of the
      row.
    - 'count' will always be a multiple of 1<<(DC.LastHostDepth+HD.ExpandFactor)
    - The returned pointer must be an ARMword-aligned pointer to the row data
    - outoffset must be used to return the bit offset within the data at which
      to start rendering.  
    - outoffset must be aligned to a host pixel boundary, i.e. multiple of
      1 << (DC.LastHostDepth + HD.ExpandFactor)

   void PDD_Name(Host_EndUpdate)(ARMul_State *state,PDD_Row *row)
    - End updating the region of the row

   void PDD_Name(Host_AdvanceRow)(ARMul_State *state,PDD_Row *row,
                                  unsigned int count)
    - Advance the row pointer by 'count' bits

   PDD_DisplayDev
    - The name to use for the const DisplayDev struct that will be generated

*/




/*

  Main struct

  A pointer to this is placed in state->Display

  TODO - 'Control' and 'HostDisplay' should probably be merged together (or
  just disposed of entirely, so everything is directly under DisplayInfo)

*/

struct PDD_Name(DisplayInfo) {
  /* Raw VIDC registers - must come first! */
  struct Vidc_Regs Vidc;

  struct {
    /* Values which get updated by VIDCPutVal */

    uint32_t DirtyPalette; /* Bit flags of which palette entries have been modified */
    bool ModeChanged; /* Set if any registers change which may require the host to change mode. Remains set until valid mode is available from host (suspends all display output) */
    bool ForceRefresh; /* Set if the entire screen needs redrawing */

    /* Values that must only get updated by the event queue/screen blit code */
    
    bool DMAEn; /* Whether video DMA is enabled for this frame */
    int LastHostWidth,LastHostHeight,LastHostDepth,LastHostHz; /* Values we used to request host mode */
    int BitWidth; /* Width of display area, in bits */
    uint16_t VIDC_CR; /* Control register value in use for this frame */
    uint32_t Vptr; /* DMA pointer, in bits, as offset from start of phys RAM */
    int FrameSkip; /* Current frame skip counter */

    /* DisplayDev_AutoUpdateFlags logic */

    int Auto_FrameCount; /* How many frames have passed */
    int Auto_ForceRefresh; /* How many frames caused a forced refresh */
  } Control;

  struct {
    /* The host must update these on calls to Host_ChangeMode */
    int Width,Height,XScale,YScale; /* Host display mode */
    ARMword *ExpandTable; /* Expansion table to use, 0 for none */
    unsigned int ExpandFactor;

    /* The core handles these */
    int XOffset,YOffset; /* X & Y offset of first display pixel in host */
    uint32_t UpdateFlags[(512*1024)/UPDATEBLOCKSIZE]; /* Update flags to track display writes */
  } HostDisplay;
};


/*

  General macros

*/

#ifdef DISPLAYINFO
#undef DISPLAYINFO
#endif
#define DISPLAYINFO (*((struct PDD_Name(DisplayInfo) *) state->Display))

#ifdef HOSTDISPLAY
#undef HOSTDISPLAY
#endif
#define HOSTDISPLAY (DISPLAYINFO.HostDisplay)

#ifdef DISPLAYCONTROL
#undef DISPLAYCONTROL
#endif
#define DISPLAYCONTROL (DISPLAYINFO.Control)

#ifdef DC
#undef DC
#endif
#define DC DISPLAYCONTROL

#ifdef HD
#undef HD
#endif
#define HD HOSTDISPLAY

#define VideoRelUpdateAndForce(flag, writeto, from) \
{\
  if ((writeto) != (from)) { \
    (writeto) = (from);\
    flag = true;\
  };\
};

#define ROWFUNC_FORCE 0x1 /* Force row to be fully redrawn */
#define ROWFUNC_UPDATED 0x2 /* Flag used to indicate whether anything was done */
#define ROWFUNC_UNALIGNED 0x4 /* Flag that gets set if we know we can't use the byte-aligned rowfuncs */

/*

  Row output for 1X horizontal scaling to same-depth display buffer

  Version for DisplayDev_UseUpdateFlags == 1

*/
       
static inline int PDD_Name(RowFunc1XSameBitAligned)(ARMul_State *state,PDD_Row drow,int flags)
{
  uint32_t Vptr = DC.Vptr;
  uint32_t Vstart = MEMC.Vstart<<7;
  uint32_t Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  const ARMword *RAM = MEMC.PhysRam;
  int Remaining = DC.BitWidth;

  /* Sanity checks to avoid looping forever */
  if((Vptr >= Vend) || (Vstart >= Vend))
    return 0;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    int Available = MIN(Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));

    if((flags & ROWFUNC_FORCE) || (HD.UpdateFlags[FlagsOffset] != MEMC.UpdateFlags[FlagsOffset]))
    {
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      int outoffset;
      ARMword *out = PDD_Name(Host_BeginUpdate)(state,&drow,Available,&outoffset);
      BitCopy(out,outoffset,RAM+(Vptr>>5),Vptr&0x1f,Available);
      PDD_Name(Host_EndUpdate)(state,&drow);
    }
    PDD_Name(Host_AdvanceRow)(state,&drow,Available);
    Vptr += Available;
    Remaining -= Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }

  DC.Vptr = Vptr;

  return (flags & ROWFUNC_UPDATED);
}

static inline int PDD_Name(RowFunc1XSameByteAligned)(ARMul_State *state,PDD_Row drow,int flags)
{
  uint32_t Vptr = DC.Vptr>>3;
  uint32_t Vstart = MEMC.Vstart<<4;
  uint32_t Vend = (MEMC.Vend+1)<<4; /* Point to pixel after end */
  const uint8_t *RAM = (uint8_t *) MEMC.PhysRam;
  int Remaining = DC.BitWidth>>3;

  /* Sanity checks to avoid looping forever */
  if((Vptr >= Vend) || (Vstart >= Vend))
    return 0;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/UPDATEBLOCKSIZE;
    int Available = MIN(Remaining,MIN(((FlagsOffset+1)*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));

    if((flags & ROWFUNC_FORCE) || (HD.UpdateFlags[FlagsOffset] != MEMC.UpdateFlags[FlagsOffset]))
    {
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      int outoffset;
      ARMword *out = PDD_Name(Host_BeginUpdate)(state,&drow,Available<<3,&outoffset);
      ByteCopy(((uint8_t *)out)+(outoffset>>3),RAM+Vptr,Available);
      PDD_Name(Host_EndUpdate)(state,&drow);
    }
    PDD_Name(Host_AdvanceRow)(state,&drow,Available<<3);
    Vptr += Available;
    Remaining -= Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }

  DC.Vptr = Vptr<<3;

  return (flags & ROWFUNC_UPDATED);
}

/*

  Row output via ExpandTable

  Version for DisplayDev_UseUpdateFlags == 1

*/

static inline int PDD_Name(RowFuncExpandTable)(ARMul_State *state,PDD_Row drow,int flags)
{
  uint32_t Vptr = DC.Vptr;
  uint32_t Vstart = MEMC.Vstart<<7;
  uint32_t Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  const ARMword *RAM = MEMC.PhysRam;
  int Remaining = DC.BitWidth;

  /* Sanity checks to avoid looping forever */
  if((Vptr >= Vend) || (Vstart >= Vend))
    return 0;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    int Available = MIN(Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));

    if((flags & ROWFUNC_FORCE) || (HD.UpdateFlags[FlagsOffset] != MEMC.UpdateFlags[FlagsOffset]))
    {
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      int outoffset;
      ARMword *out = PDD_Name(Host_BeginUpdate)(state,&drow,Available<<HD.ExpandFactor,&outoffset);
      BitCopyExpand(out,outoffset,RAM+(Vptr>>5),Vptr&0x1f,Available,HD.ExpandTable,1<<DC.LastHostDepth,HD.ExpandFactor);
      PDD_Name(Host_EndUpdate)(state,&drow);
    }
    PDD_Name(Host_AdvanceRow)(state,&drow,Available<<HD.ExpandFactor);
    Vptr += Available;
    Remaining -= Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }

  DC.Vptr = Vptr;

  return (flags & ROWFUNC_UPDATED);
}

/*

  Row output for 1X horizontal scaling to same-depth display buffer

  Version for DisplayDev_UseUpdateFlags == 0

*/

static inline void PDD_Name(RowFunc1XSameBitAlignedNoFlags)(ARMul_State *state,PDD_Row drow)
{
  uint32_t Vptr = DC.Vptr;
  uint32_t Vstart = MEMC.Vstart<<7;
  uint32_t Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  const ARMword *RAM = MEMC.PhysRam;
  int Remaining = DC.BitWidth;

  /* Sanity checks to avoid looping forever */
  if((Vptr >= Vend) || (Vstart >= Vend))
    return;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  while(Remaining > 0)
  {
    int Available = MIN(Remaining,Vend-Vptr);

    /* Process the pixels in this region, stopping at end of row/Vend */
    int outoffset;
    ARMword *out = PDD_Name(Host_BeginUpdate)(state,&drow,Available,&outoffset);
    BitCopy(out,outoffset,RAM+(Vptr>>5),Vptr&0x1f,Available);
    PDD_Name(Host_EndUpdate)(state,&drow);

    PDD_Name(Host_AdvanceRow)(state,&drow,Available);
    Vptr += Available;
    Remaining -= Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }

  DC.Vptr = Vptr;
}

static inline void PDD_Name(RowFunc1XSameByteAlignedNoFlags)(ARMul_State *state,PDD_Row drow)
{
  uint32_t Vptr = DC.Vptr>>3;
  uint32_t Vstart = MEMC.Vstart<<4;
  uint32_t Vend = (MEMC.Vend+1)<<4; /* Point to pixel after end */
  const uint8_t *RAM = (uint8_t *) MEMC.PhysRam;
  int Remaining = DC.BitWidth>>3;

  /* Sanity checks to avoid looping forever */
  if((Vptr >= Vend) || (Vstart >= Vend))
    return;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  while(Remaining > 0)
  {
    int Available = MIN(Remaining,Vend-Vptr);

    /* Process the pixels in this region, stopping at end of row/update block/Vend */
    int outoffset;
    ARMword *out = PDD_Name(Host_BeginUpdate)(state,&drow,Available<<3,&outoffset);
    ByteCopy(((uint8_t *)out)+(outoffset>>3),RAM+Vptr,Available);
    PDD_Name(Host_EndUpdate)(state,&drow);

    PDD_Name(Host_AdvanceRow)(state,&drow,Available<<3);
    Vptr += Available;
    Remaining -= Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }

  DC.Vptr = Vptr<<3;
}

/*

  Row output via ExpandTable

  Version for DisplayDev_UseUpdateFlags == 0

*/

static inline void PDD_Name(RowFuncExpandTableNoFlags)(ARMul_State *state,PDD_Row drow)
{
  uint32_t Vptr = DC.Vptr;
  uint32_t Vstart = MEMC.Vstart<<7;
  uint32_t Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  const ARMword *RAM = MEMC.PhysRam;
  int Remaining = DC.BitWidth;

  /* Sanity checks to avoid looping forever */
  if((Vptr >= Vend) || (Vstart >= Vend))
    return;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  while(Remaining > 0)
  {
    int Available = MIN(Remaining,Vend-Vptr);

    /* Process the pixels in this region, stopping at end of row/update block/Vend */
    int outoffset;
    ARMword *out = PDD_Name(Host_BeginUpdate)(state,&drow,Available<<HD.ExpandFactor,&outoffset);
    BitCopyExpand(out,outoffset,RAM+(Vptr>>5),Vptr&0x1f,Available,HD.ExpandTable,1<<DC.LastHostDepth,HD.ExpandFactor);
    PDD_Name(Host_EndUpdate)(state,&drow);

    PDD_Name(Host_AdvanceRow)(state,&drow,Available<<HD.ExpandFactor);
    Vptr += Available;
    Remaining -= Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }

  DC.Vptr = Vptr;
}


/*

  EventQ funcs

*/

static void PDD_Name(EventFunc)(ARMul_State *state,CycleCount nowtime)
{
  /* Assuming a multiplier of 2, these are the required clock dividers
     (selected via bottom two bits of VIDC.ControlReg): */
  static const uint_fast8_t ClockDividers[4] = {
  /* Source rates:     24.0MHz     25.0MHz      36.0MHz */
    6, /* 1/3      ->   8.0MHz      8.3MHz      12.0MHz */
    4, /* 1/2      ->  12.0MHz     12.5MHz      18.0MHz */
    3, /* 2/3      ->  16.0MHz     16.6MHz      24.0MHz */
    2, /* 1/1      ->  24.0MHz     25.0MHz      36.0MHz */
  };

  /* Trigger VSync interrupt */
  DisplayDev_VSync(state);

  const uint32_t NewCR = VIDC.ControlReg;
  const uint32_t ClockIn = 2*DisplayDev_GetVIDCClockIn();
  const uint8_t ClockDivider = ClockDividers[NewCR&3]; 

  /* Work out when to reschedule ourselves */
  uint32_t FramePeriod = (VIDC.Horiz_Cycle*2+2)*(VIDC.Vert_Cycle+1);
  CycleCount framelength = (((uint64_t) ARMul_EmuRate)*FramePeriod)*ClockDivider/ClockIn;
  framelength = MAX(framelength,1000);
  EventQ_Reschedule(state,nowtime+framelength,PDD_Name(EventFunc),EventQ_Find(state,PDD_Name(EventFunc)));

  if(DisplayDev_UseUpdateFlags)
  {
    /* Handle frame skip */
    if(DC.FrameSkip--)
    {
      return;
    }
    DC.FrameSkip = DisplayDev_FrameSkip;
  }

  /* Ensure mode changes if pixel clock changed */
  DC.ModeChanged |= (DC.VIDC_CR & 3) != (NewCR & 3);

  /* Force full refresh if DMA just toggled on/off */
  bool newDMAEn = (MEMC.ControlReg>>10)&1;
  bool DMAToggle = (newDMAEn ^ DC.DMAEn);
  DC.ForceRefresh |= DMAToggle;
  DC.DMAEn = newDMAEn;

  /* Ensure full palette rebuild on BPP change */
  if((DC.VIDC_CR & 0xc) != (NewCR & 0xc))
  {
    DC.DirtyPalette = 65535;
  }

  DC.VIDC_CR = NewCR;

  int Depth = (NewCR>>2)&0x3;
  int Width = (VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int Height = (VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart);
  int BPP = 1<<Depth;
  DC.BitWidth = Width*BPP;

  /* Handle any mode changes */
  if(DC.ModeChanged)
  {
    /* Work out new screen parameters
       TODO - Using display area isn't always appropriate, e.g. Stunt Racer 2000
       does some screen transitions by adjusting the display area height over
       time.
       Determining screen size via the borders would be better, but wouldn't
       allow the host to hide the borders and so use better display modes/scale
       factors. */
    if(Height <= 0)
    {
      /* Display output has been forced off by setting end addr before start
         Try using border size instead */
      Height = VIDC.Vert_BorderEnd-VIDC.Vert_BorderStart;
    }
    int FrameRate = ClockIn/(FramePeriod*ClockDivider);
    
    if((Width != DC.LastHostWidth) || (Height != DC.LastHostHeight) || (FrameRate != DC.LastHostHz) || (Depth != DC.LastHostDepth))
    {
      fprintf(stderr,"New mode: %dx%d, %dHz (CR %x ClockIn %dMhz)\n",Width,Height,FrameRate,NewCR,(int)(ClockIn/2000000));
      /* Try selecting new mode */
      if((Width < 1) || (Height < 1))
      {
        /* Bad mode; skip rendering */
        return;
      }
      
      DC.LastHostWidth = Width;
      DC.LastHostHeight = Height;
      DC.LastHostHz = FrameRate;
      DC.LastHostDepth = Depth;
      PDD_Name(Host_ChangeMode)(state,Width,Height,Depth,FrameRate);

      /* Calculate display offsets, for start of first display pixel */
      HD.XOffset = (HD.Width-Width*HD.XScale)/2;
      HD.YOffset = (HD.Height-Height*HD.YScale)/2;
      DC.ForceRefresh = true;
    }
    DC.ModeChanged = false;
    DC.DirtyPalette = -1;
  }

  /* Update AutoUpdateFlags */
  if(DisplayDev_AutoUpdateFlags)
  {
    DC.Auto_FrameCount++;
    if(DC.ForceRefresh)
      DC.Auto_ForceRefresh++;
    if(DisplayDev_UseUpdateFlags)
    {
      /* Assuming refresh rate of 50Hz, disable UpdateFlags if we've been
         running at >=5fps for the last 5 seconds. 5fps is a bit low, but it
         allows slow games like Wolf 3D to be detected (assuming you're on a
         slow host machine!) */
      if(DC.Auto_FrameCount >= 250)
      {
        if(DC.Auto_ForceRefresh >= 25)
        {
          /* Disable */
          DisplayDev_UseUpdateFlags = 0;
          DisplayDev_FrameSkip = DC.Auto_FrameCount/DC.Auto_ForceRefresh;
          ARMul_RebuildFastMap(state);
        }
        DC.Auto_FrameCount = 0;
        DC.Auto_ForceRefresh = 0;
      }
    }
    else
    {
      /* Assuming refresh rate of 50Hz, enable UpdateFlags if we've been
         running at <5fps for the last second. */
      if(DC.Auto_FrameCount >= 50)
      {
        if(DC.Auto_ForceRefresh < 5)
        {
          /* Enable */        
          DisplayDev_UseUpdateFlags = 1;
          DisplayDev_FrameSkip = 0;
          ARMul_RebuildFastMap(state);
          /* Ensure the updateflags get reset */
          DC.ForceRefresh = true;
          DC.FrameSkip = 0;
        }
        else
        {
          /* Adjust frameskip value */
          DisplayDev_FrameSkip = DC.Auto_FrameCount/DC.Auto_ForceRefresh;
        }
        DC.Auto_FrameCount = 0;
        DC.Auto_ForceRefresh = 0;
      }
    }
  }

  /* Update host palette */
  if(DC.DirtyPalette & 0x10000)
  {
    PDD_Name(Host_SetBorderColour)(state,VIDC.BorderCol);
    DC.DirtyPalette -= 0x10000;
  }
  if(DC.DirtyPalette)
  {
    int i;
    if(Depth == 3)
    {
      for(i=0;i<16;i++)
      {
        if(DC.DirtyPalette & (1<<i))
        {
          int j;
          /* Deal with the funky 8bpp palette */
          uint_fast16_t Base = VIDC.Palette[i] & 0x1737; /* Only these bits of the palette entry are used in 8bpp modes */
          static const uint_fast16_t ExtraPal[16] = {
            0x000, 0x008, 0x040, 0x048, 0x080, 0x088, 0x0c0, 0x0c8,
            0x800, 0x808, 0x840, 0x848, 0x880, 0x888, 0x8c0, 0x8c8
          };
          for(j=0;j<16;j++)
          {
            PDD_Name(Host_SetPaletteEntry)(state,i+(j<<4),Base | ExtraPal[j]);
          }
        }
      }
    }
    else
    {
      int num = 1<<BPP;
      for(i=0;i<num;i++)
      {
        if(DC.DirtyPalette & (1<<i))
        {
          PDD_Name(Host_SetPaletteEntry)(state,i,VIDC.Palette[i]);
        }
      }
    }
    DC.DirtyPalette = 0;
  }

  /* Set up DMA */
  DC.Vptr = MEMC.Vinit<<7;

  /* Render */
  if(newDMAEn)
  {
    int i;
    int flags = (DC.ForceRefresh?ROWFUNC_FORCE:0);

    /* We can test these values once here, so that it's only output alignment
       that we need to worry about during the loop */
    if((DC.Vptr & 0x7) || ((Width*BPP)&0x7))
      flags |= ROWFUNC_UNALIGNED;

    if(DisplayDev_UseUpdateFlags)
    {
      for(i=0;i<Height;i++)
      {
        int hoststart = i*HD.YScale+HD.YOffset;
        int hostend = hoststart+HD.YScale;
        if(hoststart < 0)
          hoststart = 0;
        if(hostend > HD.Height)
          hostend = HD.Height;
        ARMword Vptr = DC.Vptr;
        while(hoststart < hostend)
        {
          DC.Vptr = Vptr;
          int alignment;
          int updated;
          PDD_Row hrow = PDD_Name(Host_BeginRow)(state,hoststart++,HD.XOffset,&alignment);
          if(HD.ExpandTable)
          {
            updated = PDD_Name(RowFuncExpandTable)(state,hrow,flags);
          }
          else if(!(flags & ROWFUNC_UNALIGNED) && !(alignment & 0x7))
          {
            updated = PDD_Name(RowFunc1XSameByteAligned)(state,hrow,flags);
          }
          else
          {
            updated = PDD_Name(RowFunc1XSameBitAligned)(state,hrow,flags);
          }
          PDD_Name(Host_EndRow)(state,&hrow);
          if(updated)
            flags |= ROWFUNC_UPDATED;
          else
            break;
        }
      }
    
      /* Update UpdateFlags */
      if(flags & ROWFUNC_UPDATED)
      {
        /* Only need to update between MIN(Vinit,Vstart) and Vend */
        int start = MIN(MEMC.Vinit,MEMC.Vstart)/(UPDATEBLOCKSIZE/16);
        int end = (MEMC.Vend/(UPDATEBLOCKSIZE/16))+1;
        memcpy(HD.UpdateFlags+start,MEMC.UpdateFlags+start,(end-start)*sizeof(uint32_t));
      }
    }
    else
    {
      /* Only update if forced, or frameskip has run out */
      if((flags & ROWFUNC_FORCE) || (!DC.FrameSkip))
      {
        DC.FrameSkip = DisplayDev_FrameSkip;

        for(i=0;i<Height;i++)
        {
          int hoststart = i*HD.YScale+HD.YOffset;
          int hostend = hoststart+HD.YScale;
          if(hoststart < 0)
            hoststart = 0;
          if(hostend > HD.Height)
            hostend = HD.Height;
          ARMword Vptr = DC.Vptr;
          while(hoststart < hostend)
          {
            DC.Vptr = Vptr;
            int alignment;
            PDD_Row hrow = PDD_Name(Host_BeginRow)(state,hoststart++,HD.XOffset,&alignment);
            if(HD.ExpandTable)
            {
              PDD_Name(RowFuncExpandTableNoFlags)(state,hrow);
            }
            else if(!(flags & ROWFUNC_UNALIGNED) && !(alignment & 0x7))
            {
              PDD_Name(RowFunc1XSameByteAlignedNoFlags)(state,hrow);
            }
            else
            {
              PDD_Name(RowFunc1XSameBitAlignedNoFlags)(state,hrow);
            }
            PDD_Name(Host_EndRow)(state,&hrow);
          }
        }
      }
      else
      {
        DC.FrameSkip--;
      }
    }
  }
  else if(DMAToggle)
  {
    /* DMA just turned off, fill screen with border colour */
    /* TODO - Cope with other situations, e.g. changes in display area size */
    PDD_Name(Host_DrawBorderRect)(state,HD.XOffset,HD.YOffset,Width*HD.XScale,Height*HD.YScale);
  }
  DC.ForceRefresh = false;

  /* Update host */
  PDD_Name(Host_PollDisplay)(state);

  /* Done! */
}

/*

  VIDC/IOEB write handler

*/

static void PDD_Name(VIDCPutVal)(ARMul_State *state,ARMword address, ARMword data,bool bNw) {
  uint32_t addr, val;

  addr=(data>>24) & 255;
  val=data & 0xffffff;

  if (!(addr & 0xc0)) {
    uint_fast8_t Log;
    uint_fast16_t Phy;

    Log=(addr>>2) & 15;
    Phy = (val & 0x1fff);
    if(VIDC.Palette[Log] != Phy)
    {
      VIDC.Palette[Log] = Phy;
      DC.DirtyPalette |= (1<<Log);
    }
    return;
  };

  addr&=~3;
  switch (addr) {
    case 0x40: /* Border col */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC border colour write val=0x%x\n",val);
#endif
      val &= 0x1fff;
      if(VIDC.BorderCol != val)
      {
        VIDC.BorderCol = val;
        DC.DirtyPalette |= 0x10000;
      }
      break;

    case 0x44: /* Cursor palette log col 1 */
    case 0x48: /* Cursor palette log col 2 */
    case 0x4c: /* Cursor palette log col 3 */
      addr = (addr-0x44)>>2;
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC cursor log col %d write val=0x%x\n",addr+1,val);
#endif
      VIDC.CursorPalette[addr] = val & 0x1fff;
      break;

    case 0x60: /* Stereo image reg 7 */
    case 0x64: /* Stereo image reg 0 */
    case 0x68: /* Stereo image reg 1 */
    case 0x6c: /* Stereo image reg 2 */
    case 0x70: /* Stereo image reg 3 */
    case 0x74: /* Stereo image reg 4 */
    case 0x78: /* Stereo image reg 5 */
    case 0x7c: /* Stereo image reg 6 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC stereo image reg write val=0x%x\n",val);
#endif
      val &= 7;
      addr = ((addr-0x64)>>2)&0x7;
      if(VIDC.StereoImageReg[addr] != val)
      {
        VIDC.StereoImageReg[addr] = val;
#ifdef SOUND_SUPPORT
        Sound_StereoUpdated(state);
#endif
      }
      break;

    case 0x80:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz cycle register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Horiz_Cycle,(val>>14) & 0x3ff);
      break;

    case 0x84:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz sync width register val=%d\n",val>>14);
#endif
      VIDC.Horiz_SyncWidth = (val>>14) & 0x3ff;
      break;

    case 0x88:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz border start register val=%d\n",val>>14);
#endif
      VIDC.Horiz_BorderStart = (val>>14) & 0x3ff;
      break;

    case 0x8c:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz display start register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Horiz_DisplayStart,(val>>14) & 0x3ff);
      break;

    case 0x90:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz display end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Horiz_DisplayEnd,(val>>14) & 0x3ff);
      break;

    case 0x94:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC horizontal border end register val=%d\n",val>>14);
#endif
      VIDC.Horiz_BorderEnd = (val>>14) & 0x3ff;
      break;

    case 0x98:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC horiz cursor start register val=%d\n",val>>13);
#endif
      VIDC.Horiz_CursorStart=(val>>13) & 0x7ff;
      break;

    case 0x9c:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC horiz interlace register val=%d\n",val>>14);
#endif
      VIDC.Horiz_Interlace = (val>>14) & 0x3ff;
      break;

    case 0xa0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert cycle register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Vert_Cycle,(val>>14) & 0x3ff);
      break;

    case 0xa4:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert sync width register val=%d\n",val>>14);
#endif
      VIDC.Vert_SyncWidth = (val>>14) & 0x3ff;
      break;

    case 0xa8:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert border start register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Vert_BorderStart,((val>>14) & 0x3ff));
      break;

    case 0xac:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert disp start register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Vert_DisplayStart,((val>>14) & 0x3ff));
      break;

    case 0xb0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert disp end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Vert_DisplayEnd,(val>>14) & 0x3ff);
      break;

    case 0xb4:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert Border end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.ModeChanged,VIDC.Vert_BorderEnd,((val>>14) & 0x3ff));
      break;

    case 0xb8:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert cursor start register val=%d\n",val>>14);
#endif
      VIDC.Vert_CursorStart=(val>>14) & 0x3ff;
      break;

    case 0xbc:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert cursor end register val=%d\n",val>>14);
#endif
      VIDC.Vert_CursorEnd=(val>>14) & 0x3ff;
      break;

    case 0xc0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Sound freq register val=%d\n",val);
#endif
      val &= 0xff;
      if(VIDC.SoundFreq != val)
      {
        VIDC.SoundFreq=val;
#ifdef SOUND_SUPPORT
        Sound_SoundFreqUpdated(state);
#endif
      }
      break;

    case 0xe0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Control register val=0x%x\n",val);
#endif
      VIDC.ControlReg = val & 0xffff;
      break;

    default:
      fprintf(stderr,"Write to unknown VIDC register reg=0x%x val=0x%x\n",addr,val);
      break;

  }; /* Register switch */
}; /* PutValVIDC */

static void PDD_Name(IOEBCRWrite)(ARMul_State *state,ARMword data) {
  DC.ModeChanged = true;
};

/*

  DisplayDev wrapper

*/

static int PDD_Name(Init)(ARMul_State *state,const struct Vidc_Regs *Vidc)
{
  state->Display = calloc(sizeof(struct PDD_Name(DisplayInfo)),1);
  if(!state->Display) {
    fprintf(stderr,"Failed to allocate DisplayInfo\n");
    return -1;
  }

  VIDC = *Vidc;

  DC.ModeChanged = true;
  DC.LastHostWidth = DC.LastHostHeight = DC.LastHostHz = DC.LastHostDepth = -1;
  DC.DirtyPalette = 65535;
  DC.VIDC_CR = 0;
  DC.DMAEn = false;

  memset(HOSTDISPLAY.UpdateFlags,0,sizeof(HOSTDISPLAY.UpdateFlags)); /* Initial value in MEMC.UpdateFlags is 1 */   

  /* Schedule first update event */
  EventQ_Insert(state,ARMul_Time+100,PDD_Name(EventFunc));

  return 0;
}

static void PDD_Name(Shutdown)(ARMul_State *state)
{
  int idx = EventQ_Find(state,PDD_Name(EventFunc));
  if(idx >= 0)
    EventQ_Remove(state,idx);
  else
  {
    ControlPane_Error(EXIT_FAILURE,"Couldn't find PDD_Name(EventFunc)!\n");
  }
  free(state->Display);
  state->Display = NULL;
}

static void PDD_Name(DAGWrite)(ARMul_State *state,int reg,ARMword val)
{
  /* Vinit, Vstart & Vend changes require a full screen refresh */
  DC.ForceRefresh = true;
}

const DisplayDev PDD_DisplayDev = {
  PDD_Name(Init),
  PDD_Name(Shutdown),
  PDD_Name(VIDCPutVal),
  PDD_Name(DAGWrite),
  PDD_Name(IOEBCRWrite),
};

/*

  The end

*/

#undef VideoRelUpdateAndForce
#undef ROWFUNC_FORCE
#undef ROWFUNC_UPDATEFLAGS
#undef ROWFUNC_UPDATED

