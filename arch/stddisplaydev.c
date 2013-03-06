/*
   arch/stddisplaydev.c

   (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>
   Based in part on original video code by David Alan Gilbert et al 

   Part of Arcem, covered under the GNU GPL, see file COPYING for details

   This is the core implementation of the "standard display driver". It aims to
   provide scanline-accurate timing of display updates, using the interface
   described below to update the host display buffer. It's designed to be used
   with hosts that support true/full colour displays (i.e. non-palettised),
   although it can also be used with palettised ones if a fixed palette is used.

   Cursor display is expected to be fully handled by the host, e.g. via
   hardware overlay or manually rendering a (masked) image ontop of the main
   display.
*/

/*
   This file is intended to be #included directly by another source file in
   order to generate a working display driver.

   Before including the file, be sure to define the following
   types/symbols/macros:

   SDD_HostColour
    - Data type used to store a colour value. E.g.
      "typedef unsigned short SDD_HostColour".

   SDD_Name(x)
    - Macro used to convert symbol name 'x' into an instance-specific version
      of the symbol. e.g.
      "#define SDD_Name(x) MySDD_##x"

   void SDD_Name(Host_PollDisplay)(ARMul_State *state)
    - A function that the driver will call at the start of each frame.

   SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,uint_fast16_t col)
    - A function that the driver will call in order to convert a 13-bit VIDC
      physical colour into a host colour.

   void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,
                                  int hz)
    - A function that the driver will call whenever the display timings have
      changed enough to warrant a mode change.
    - The implementation must change to the most appropriate display mode
      available and fill in the Width, Height, XScale and YScale members of the
      HostDisplay struct to reflect the new display parameters.
    - Currently, failure to find a suitable mode isn't supported - however it
      shouldn't be too hard to keep the screen blanked (not ideal) by keeping
      DC.ModeChanged set to 1

   SDD_RowsAtOnce
    - The number of source rows to process per update. This can be a non-const
      variable if you want, so it can be tweaked on the fly to tune performance

   SDD_Row
    - A data type that acts as an iterator/accessor for a screen row. It can be
      anything from a simple *SDD_HostColour (for hosts with direct framebuffer
      access, e.g. RISC OS), or something more complex like a struct which keeps
      track of the current drawing coordinates (e.g. the truecolour X11 driver)
    - SDD_Row instances must be able to cope with being passed by value as
      function parameters.

   SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
    - Function to return a SDD_Row instance suitable for accessing the indicated
      row, starting from the given X offset

   void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row)
    - Function to end the use of a SDD_Row
    - Where a SDD_Row has been copied via pass-by-value, currently only the
      original instance will have Host_EndRow called on it.

   void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,
                                   unsigned int count)
    - Function called when the driver is about to write to 'count' pixels of the
      row. Implementations could use it for tracking dirty regions in the
      display window.

   void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row)
    - Function called once the driver has finished the write operation

   void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,
                                  unsigned int count)
    - Function to skip forwards 'count' pixels in the row

   void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,
                                  SDD_HostColour col)
    - Function to write a single pixel and advance to the next location. Only
      called between BeginUpdate & EndUpdate.

   void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,
                                   SDD_HostColour col,unsigned int count)
    - Function to fill N adjacent pixels with the same colour. 'count' may be
      zero. Only called between BeginUpdate & EndUpdate.

   SDD_DisplayDev
    - The name to use for the const DisplayDev struct that will be generated
    
   SDD_Stats
    - Define this to enable the stats code.

*/




/*

  Stats

  Note - don't enable for more than one SDD instance at once, the symbol names it uses aren't unique

*/

#ifdef SDD_Stats
#define VIDEO_STAT(STAT,COND,AMT) if(COND) {vidstats[vidstat_##STAT] += AMT;}
#else
#define VIDEO_STAT(STAT,COND,AMT) ((void)0)
#endif

#ifdef SDD_Stats
enum vidstat {
  vidstat_BorderRedraw,
  vidstat_BorderRedrawForced,
  vidstat_BorderRedrawColourChanged,
  vidstat_DisplayRowRedraw,
  vidstat_DisplayRedraw,
  vidstat_DisplayRedrawForced,
  vidstat_DisplayRedrawUpdated,
  vidstat_DisplayBits,
  vidstat_DisplayRowForce,
  vidstat_DisplayFullForce,
  vidstat_DisplayRows,
  vidstat_DisplayFrames,
  vidstat_ForceRefreshDMA,
  vidstat_ForceRefreshBPP,
  vidstat_RefreshFlagsVinit,
  vidstat_RefreshFlagsPalette,
  vidstat_MAX,
};
static uint32_t vidstats[vidstat_MAX];
static const char *vidstatnames[vidstat_MAX] = {
 "BorderRedraw: Total border redraws",
 "BorderRedrawForced: Total forced border redraws",
 "BorderRedrawColourChanged: Total border redraws due to colour change",
 "DisplayRowRedraw: Number of rows where display data was updated",
 "DisplayRedraw: Number of blocks/sections updated",
 "DisplayRedrawForced: Number of forced blocks/sections updated",
 "DisplayRedrawUpdated: Number of blocks/sections updated",
 "DisplayBits: Number of display bits updated",
 "DisplayRowForce: Number of display row redraws due to DC.RefreshFlags",
 "DisplayFullForce: Number of display row redraws due to DC.ForceRefresh",
 "DisplayRows: Total number of rows processed",
 "DisplayFrames: Total number of frames processed",
 "ForceRefreshDMA: Frames where ForceRefresh was set due to DMA enable toggle",
 "ForceRefreshBPP: Frames where ForceRefresh was set due to BPP change",
 "RefreshFlagsVinit: Frames where RefreshFlags were set due to Vinit change",
 "RefreshFlagsPalette: Palette writes causing RefreshFlags to be set",
};

static void vidstats_Dump(const char *c)
{
  fprintf(stderr,"%s\n",c);
  int i;
  for(i=0;i<vidstat_MAX;i++)
  {
    fprintf(stderr,"%12u %s\n",vidstats[i],vidstatnames[i]);
    vidstats[i] = 0;
  }
}
#endif

/*

  Main struct

  A pointer to this is placed in state->Display

  TODO - 'Control' and 'HostDisplay' should probably be merged together (or
  just disposed of entirely, so everything is directly under DisplayInfo)

*/

struct SDD_Name(DisplayInfo) {
  /* Raw VIDC registers - must come first! */
  struct Vidc_Regs Vidc;

  struct {
    /* Values which get updated by VIDCPutVal */

    uint_fast16_t DirtyPalette; /* Bit flags of which palette entries have been modified */
    bool ModeChanged; /* Set if any registers change which may require the host to change mode. Remains set until valid mode is available from host (suspends all display output) */

    /* Values that must only get updated by the event queue/screen blit code */
    
    bool ForceRefresh; /* True for the entire frame if the mode has just changed */
    bool DMAEn; /* Whether video DMA is enabled for this frame */
    bool FLYBK; /* Flyback signal (i.e. whether we've triggered VSync IRQ this frame) */ 
    int LastHostWidth,LastHostHeight,LastHostHz; /* Values we used to request host mode */
    int LastRow; /* Row last event was scheduled to run up to */
    int NextRow; /* Row next event is scheduled to run up to */
    int MaxRow; /* Row to stop at for this frame */
    uint16_t VIDC_CR; /* Control register value in use for this frame */
    uint32_t LineRate; /* Line rate, measured in EmuRate clock cycles */
    uint32_t Vptr; /* DMA pointer, in bits, as offset from start of phys RAM */
    uint32_t LastVinit; /* Last Vinit, so we can sync changes with the frame start */
    int FrameSkip; /* Current frame skip counter */

    /* DisplayDev_AutoUpdateFlags logic */

    int Auto_FrameCount; /* How many frames have passed */
    int Auto_ForceRefresh; /* How many frames caused a forced refresh */
  } Control;

  struct {
    /* The host must update these on calls to Host_ChangeMode */
    int Width,Height,XScale,YScale; /* Host display mode */

    /* The core handles these */
    int XOffset,YOffset; /* X & Y offset of first display pixel in host */
    SDD_HostColour BorderCol; /* VIDC.Border colour in host format */ 
    SDD_HostColour Palette[256]; /* Host palette */
    SDD_HostColour BorderCols[1024]; /* Last border colour used for each scanline */
    uint32_t RefreshFlags[1024/32]; /* Bit flags of which display scanlines need full refresh due to Vstart/Vend/palette changes */
    uint32_t UpdateFlags[1024][(512*1024)/UPDATEBLOCKSIZE]; /* Flags for each scanline (8MB of flags - ouch!) */
  } HostDisplay;
};


/*

  General macros

*/

#ifdef DISPLAYINFO
#undef DISPLAYINFO
#endif
#define DISPLAYINFO (*((struct SDD_Name(DisplayInfo) *) state->Display))

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


/*

  Palette update functions

*/

static inline void SDD_Name(PaletteUpdate)(ARMul_State *state,SDD_HostColour *Palette,int num)
{
  /* Might be better if caller does this check? */
  if(DC.DirtyPalette)
  {
    int i;
    for(i=0;i<num;i++)
    {
      if(DC.DirtyPalette & (1<<i))
      {
        Palette[i] = SDD_Name(Host_GetColour)(state,VIDC.Palette[i]);
      }
    }
    DC.DirtyPalette = 0;
  }
}

static inline void SDD_Name(PaletteUpdate8bpp)(ARMul_State *state,SDD_HostColour *Palette)
{
  /* Might be better if caller does this check? */
  if(DC.DirtyPalette)
  {
    int i;
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
          Palette[i+(j<<4)] = SDD_Name(Host_GetColour)(state,Base | ExtraPal[j]);
        }
      }
    }
    DC.DirtyPalette = 0;
  }
}

/*

  Screen output general

*/

/* Prototype of a function used for updating the display area of a row.
   'drow' is expected to already be pointing to the start of the display area
   Returns non-zero if the row was updated
*/
typedef int (*SDD_Name(RowFunc))(ARMul_State *state,int row,SDD_Row drow,int flags);
/* Alternate version for when UpdateFlags are disabled */
typedef void (*SDD_Name(RowFuncNoFlags))(ARMul_State *state,int row,SDD_Row drow);


#define ROWFUNC_FORCE 0x1 /* Force row to be fully redrawn */
#define ROWFUNC_UPDATEFLAGS 0x2 /* Update the UpdateFlags */

#define ROWFUNC_UPDATED 0x4 /* Flag used internally by rowfuncs to indicate whether anything was done */

/*

  Screen output for 1X horizontal scaling

  Version for DisplayDev_UseUpdateFlags == 1

*/

static int SDD_Name(RowFunc1bpp1X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,2);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth;

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      ARMword Bit, Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available);
      In = RAM+(Vptr>>5);
      Bit = 1<<(Vptr & 31);
      Data = *In++;
      for(i=0;i<Available;i++)
      {
        int idx = (Data & Bit)?1:0;
        SDD_Name(Host_WritePixel)(state,&drow,Palette[idx]);
        Bit <<= 1;
        if(!Bit)
        {
          Bit = 1;
          Data = *In++;
        }
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

static int SDD_Name(RowFunc2bpp1X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,4);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*2; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      uint32_t Shift;
      ARMword Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available>>1);
      In = RAM+(Vptr>>5);
      Shift = (Vptr & 31);
      Data = (*In++) >> Shift;
      for(i=0;i<Available;i+=2)
      {
        SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 3]);
        Data >>= 2;
        Shift += 2;
        if(Shift == 32)
        {
          Shift = 0;
          Data = *In++;
        }
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available>>1);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

static int SDD_Name(RowFunc4bpp1X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,16);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*4; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      uint32_t Shift;
      ARMword Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available>>2);

      /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
      In = RAM+(Vptr>>5);      
      Shift = (Vptr & 31);
      Data = (*In++) >> Shift;
      for(i=0;i<Available;i+=8)
      {
        SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xf]);
        Data >>= 4;
        SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xf]);
        Data >>= 4;
        Shift += 8;
        if(Shift == 32)
        {
          Shift = 0;
          Data = *In++;
        }        
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available>>2);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

static int SDD_Name(RowFunc8bpp1X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate8bpp)(state,Palette);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*8; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      uint32_t Shift;
      ARMword Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available>>3);

      /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
      In = RAM+(Vptr>>5);
      Shift = (Vptr & 31);
      Data = (*In++) >> Shift;
      for(i=0;i<Available;i+=16)
      {
        SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xff]);
        Data >>= 8;
        SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xff]);
        if(Shift)
        {
          Shift = 0;
          Data = *In++;
        }
        else
        {
          Shift = 16;
          Data >>= 8;
        }
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available>>3);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

/*

  Screen output for 2X horizontal scaling

  Version for DisplayDev_UseUpdateFlags == 1

*/

static int SDD_Name(RowFunc1bpp2X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,2);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth;

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      ARMword Bit, Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available<<1);
      In = RAM+(Vptr>>5);
      Bit = 1<<(Vptr & 31);
      Data = *In++;
      for(i=0;i<Available;i++)
      {
        int idx = (Data & Bit)?1:0;
        SDD_Name(Host_WritePixels)(state,&drow,Palette[idx],2);
        Bit <<= 1;
        if(!Bit)
        {
          Bit = 1;
          Data = *In++;
        }
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available<<1);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

static int SDD_Name(RowFunc2bpp2X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,4);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*2; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      uint32_t Shift;
      ARMword Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available);
      In = RAM+(Vptr>>5);
      Shift = (Vptr & 31);
      Data = (*In++) >> Shift;
      for(i=0;i<Available;i+=2)
      {
        SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 3],2);
        Data >>= 2;
        Shift += 2;
        if(Shift == 32)
        {
          Shift = 0;
          Data = *In++;
        }
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

static int SDD_Name(RowFunc4bpp2X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,16);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*4; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      uint32_t Shift;
      ARMword Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available>>1);

      /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
      In = RAM+(Vptr>>5);      
      Shift = (Vptr & 31);
      Data = (*In++) >> Shift;
      for(i=0;i<Available;i+=8)
      {
        SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xf],2);
        Data >>= 4;
        SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xf],2);
        Data >>= 4;
        Shift += 8;
        if(Shift == 32)
        {
          Shift = 0;
          Data = *In++;
        }        
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available>>1);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

static int SDD_Name(RowFunc8bpp2X)(ARMul_State *state,int row,SDD_Row drow,int flags)
{
  int i, Remaining, startRemain;
  uint32_t Vptr, Vstart, Vend, startVptr;
  const ARMword *RAM;
  const uint32_t *MEMC_UpdateFlags;
  uint32_t *HD_UpdateFlags;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate8bpp)(state,Palette);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*8; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  startVptr = Vptr;
  startRemain = Remaining;
  MEMC_UpdateFlags = MEMC.UpdateFlags;
  HD_UpdateFlags = HD.UpdateFlags[row];
  while(Remaining > 0)
  {
    uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
      
    if((flags & ROWFUNC_FORCE) || (HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]))
    {
      const ARMword *In;
      uint32_t Shift;
      ARMword Data;
      VIDEO_STAT(DisplayRedraw,1,1);
      VIDEO_STAT(DisplayRedrawForced,(flags & ROWFUNC_FORCE),1);
      VIDEO_STAT(DisplayRedrawUpdated,(HD_UpdateFlags[FlagsOffset] != MEMC_UpdateFlags[FlagsOffset]),1);
      VIDEO_STAT(DisplayBits,1,Available);
      flags |= ROWFUNC_UPDATED;
      /* Process the pixels in this region, stopping at end of row/update block/Vend */
      SDD_Name(Host_BeginUpdate)(state,&drow,Available>>2);

      /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
      In = RAM+(Vptr>>5);
      Shift = (Vptr & 31);
      Data = (*In++) >> Shift;
      for(i=0;i<Available;i+=16)
      {
        SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xff],2);
        Data >>= 8;
        SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xff],2);
        if(Shift)
        {
          Shift = 0;
          Data = *In++;
        }
        else
        {
          Shift = 16;
          Data >>= 8;
        }
      }
      SDD_Name(Host_EndUpdate)(state,&drow);
    }
    else
      SDD_Name(Host_SkipPixels)(state,&drow,Available>>2);

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
        
  /* If we updated anything, copy over the updated flags (Done last in case the same flags block is encountered multiple times in the same row) */
  if((flags & (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS)) == (ROWFUNC_UPDATED | ROWFUNC_UPDATEFLAGS))
  {
    Vptr = startVptr;
    Remaining = startRemain;
    while(Remaining > 0)
    {
      uint32_t FlagsOffset = Vptr/(8*UPDATEBLOCKSIZE);
      int Available = MIN((uint32_t)Remaining,MIN(((FlagsOffset+1)*8*UPDATEBLOCKSIZE)-Vptr,Vend-Vptr));
  
      HD_UpdateFlags[FlagsOffset] = MEMC_UpdateFlags[FlagsOffset];
      
      Remaining -= Available;      
      Vptr += Available;
      if(Vptr >= Vend)
        Vptr = Vstart;
    }
  }

  return (flags & ROWFUNC_UPDATED);
}

/*

  Screen output for 1X horizontal scaling

  Version for DisplayDev_UseUpdateFlags == 0

*/

static void SDD_Name(RowFunc1bpp1XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,2);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth;

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining);
  while(Remaining > 0)
  {
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    ARMword Bit, Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */
    In = RAM+(Vptr>>5);
    Bit = 1<<(Vptr & 31);
    Data = *In++;
    for(i=0;i<Available;i++)
    {
      int idx = (Data & Bit)?1:0;
      SDD_Name(Host_WritePixel)(state,&drow,Palette[idx]);
      Bit <<= 1;
      if(!Bit)
      {
        Bit = 1;
        Data = *In++;
      }
    }
    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

static void SDD_Name(RowFunc2bpp1XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,4);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*2; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining>>1);
  while(Remaining > 0)
  {
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    uint32_t Shift;
    ARMword Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */
    In = RAM+(Vptr>>5);
    Shift = (Vptr & 31);
    Data = (*In++) >> Shift;
    for(i=0;i<Available;i+=2)
    {
      SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 3]);
      Data >>= 2;
      Shift += 2;
      if(Shift == 32)
      {
        Shift = 0;
        Data = *In++;
      }
    }

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

static void SDD_Name(RowFunc4bpp1XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,16);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*4; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining>>2);
  while(Remaining > 0)
  {
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    uint32_t Shift;
    ARMword Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */

    /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
    In = RAM+(Vptr>>5);      
    Shift = (Vptr & 31);
    Data = (*In++) >> Shift;
    for(i=0;i<Available;i+=8)
    {
      SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xf]);
      Data >>= 4;
      SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xf]);
      Data >>= 4;
      Shift += 8;
      if(Shift == 32)
      {
        Shift = 0;
        Data = *In++;
      }        
    }

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

static void SDD_Name(RowFunc8bpp1XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate8bpp)(state,Palette);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*8; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining>>3);
  while(Remaining > 0)
  {
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    uint32_t Shift;
    ARMword Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */

    /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
    In = RAM+(Vptr>>5);
    Shift = (Vptr & 31);
    Data = (*In++) >> Shift;
    for(i=0;i<Available;i+=16)
    {
      SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xff]);
      Data >>= 8;
      SDD_Name(Host_WritePixel)(state,&drow,Palette[Data & 0xff]);
      if(Shift)
      {
        Shift = 0;
        Data = *In++;
      }
      else
      {
        Shift = 16;
        Data >>= 8;
      }
    }

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

/*

  Screen output for 2X horizontal scaling

  Version for DisplayDev_UseUpdateFlags == 0

*/

static void SDD_Name(RowFunc1bpp2XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,2);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth;

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining<<1);
  while(Remaining > 0)
  {
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    ARMword Bit, Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */
    In = RAM+(Vptr>>5);
    Bit = 1<<(Vptr & 31);
    Data = *In++;
    for(i=0;i<Available;i++)
    {
      int idx = (Data & Bit)?1:0;
      SDD_Name(Host_WritePixels)(state,&drow,Palette[idx],2);
      Bit <<= 1;
      if(!Bit)
      {
        Bit = 1;
        Data = *In++;
      }
    }

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

static void SDD_Name(RowFunc2bpp2XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,4);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*2; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining);
  while(Remaining > 0)
  {
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    uint32_t Shift;
    ARMword Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */
    In = RAM+(Vptr>>5);
    Shift = (Vptr & 31);
    Data = (*In++) >> Shift;
    for(i=0;i<Available;i+=2)
    {
      SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 3],2);
      Data >>= 2;
      Shift += 2;
      if(Shift == 32)
      {
        Shift = 0;
        Data = *In++;
      }
    }

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

static void SDD_Name(RowFunc4bpp2XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate)(state,Palette,16);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*4; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining>>1);
  while(Remaining > 0)
  {
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    uint32_t Shift;
    ARMword Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */

    /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
    In = RAM+(Vptr>>5);      
    Shift = (Vptr & 31);
    Data = (*In++) >> Shift;
    for(i=0;i<Available;i+=8)
    {
      SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xf],2);
      Data >>= 4;
      SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xf],2);
      Data >>= 4;
      Shift += 8;
      if(Shift == 32)
      {
        Shift = 0;
        Data = *In++;
      }        
    }

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

static void SDD_Name(RowFunc8bpp2XNoFlags)(ARMul_State *state,int row,SDD_Row drow)
{
  int i, Remaining;
  uint32_t Vptr, Vstart, Vend;
  const ARMword *RAM;
  SDD_HostColour *Palette = HD.Palette;
  /* Handle palette updates */
  SDD_Name(PaletteUpdate8bpp)(state,Palette);

  Vptr = DC.Vptr;
  Vstart = MEMC.Vstart<<7;
  Vend = (MEMC.Vend+1)<<7; /* Point to pixel after end */
  RAM = MEMC.PhysRam;
  Remaining = DC.LastHostWidth*8; /* Scale up to account for everything else counting in bits */

  /* Sanity checks to avoid looping forever */
  if(Vend == Vstart)
    Vend = Vstart+128;
  if(Vptr >= Vend)
    Vptr = Vstart;

  /* Process the row */
  SDD_Name(Host_BeginUpdate)(state,&drow,Remaining>>2);
  while(Remaining > 0)
  {
    /* Note: This is the number of available bits, not pixels */
    int Available = MIN((uint32_t)Remaining,Vend-Vptr);
    const ARMword *In;
    uint32_t Shift;
    ARMword Data;
    VIDEO_STAT(DisplayRedraw,1,1);
    VIDEO_STAT(DisplayBits,1,Available);
    /* Process the pixels in this region, stopping at end of row/Vend */

    /* Display will always be a multiple of 2 pixels wide, so we can simplify things a bit compared to 1/2bpp case */
    In = RAM+(Vptr>>5);
    Shift = (Vptr & 31);
    Data = (*In++) >> Shift;
    for(i=0;i<Available;i+=16)
    {
      SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xff],2);
      Data >>= 8;
      SDD_Name(Host_WritePixels)(state,&drow,Palette[Data & 0xff],2);
      if(Shift)
      {
        Shift = 0;
        Data = *In++;
      }
      else
      {
        Shift = 16;
        Data >>= 8;
      }
    }

    Remaining -= Available;      
    Vptr += Available;
    if(Vptr >= Vend)
      Vptr = Vstart;
  }
  DC.Vptr = Vptr;
  SDD_Name(Host_EndUpdate)(state,&drow);
}

/*

  Screen output other funcs

*/

static void SDD_Name(BorderRow)(ARMul_State *state,int row)
{
  int hoststart, hostend;
  /* Render a border row */
  SDD_HostColour col = HD.BorderCol;
  if(!DC.ForceRefresh && (HD.BorderCols[row] == col))
    return;
  VIDEO_STAT(BorderRedraw,1,1);
  VIDEO_STAT(BorderRedrawForced,DC.ForceRefresh,1);
  VIDEO_STAT(BorderRedrawColourChanged,(HD.BorderCols[row] != col),1);
  HD.BorderCols[row] = col;
  hoststart = (row-(VIDC.Vert_DisplayStart+1))*HD.YScale+HD.YOffset;
  hostend = hoststart + HD.YScale;
  if(hoststart < 0)
    hoststart = 0;
  if(hostend > HD.Height)
    hostend = HD.Height;
  while(hoststart < hostend)
  {
    SDD_Row drow = SDD_Name(Host_BeginRow)(state,hoststart++,0);
    SDD_Name(Host_BeginUpdate)(state,&drow,HD.Width);
    SDD_Name(Host_WritePixels)(state,&drow,col,HD.Width);
    SDD_Name(Host_EndUpdate)(state,&drow);
    SDD_Name(Host_EndRow)(state,&drow);
  }
}

static const SDD_Name(RowFunc) SDD_Name(RowFuncs)[2][4] = {
 { /* 1X horizontal scaling */
   SDD_Name(RowFunc1bpp1X),
   SDD_Name(RowFunc2bpp1X),
   SDD_Name(RowFunc4bpp1X),
   SDD_Name(RowFunc8bpp1X)
 },
 { /* 2X horizontal scaling */
   SDD_Name(RowFunc1bpp2X),
   SDD_Name(RowFunc2bpp2X),
   SDD_Name(RowFunc4bpp2X),
   SDD_Name(RowFunc8bpp2X)
 }
};

static void SDD_Name(DisplayRow)(ARMul_State *state,int row)
{
  int rowflags;
  SDD_HostColour col;
  uint32_t flags, bit;
  SDD_Row drow;
  const SDD_Name(RowFunc) *rf;
  /* Render a display row */
  int hoststart = (row-(VIDC.Vert_DisplayStart+1))*HD.YScale+HD.YOffset;
  int hostend = hoststart + HD.YScale;
  if(hoststart < 0)
    hoststart = 0;
  if(hostend > HD.Height)
    hostend = HD.Height;
  if(hoststart >= hostend)
    return;

  /* Handle border colour updates */
  rowflags = (DC.ForceRefresh?ROWFUNC_FORCE:0);
  col = HD.BorderCol;
  
  if(rowflags || (HD.BorderCols[row] != col))
  {
    int i;
    VIDEO_STAT(BorderRedraw,1,1);
    VIDEO_STAT(BorderRedrawForced,rowflags,1);
    VIDEO_STAT(BorderRedrawColourChanged,(HD.BorderCols[row] != col),1);
    HD.BorderCols[row] = col;
    for(i=hoststart;i<hostend;i++)
    {
      int displaywidth, rightborder;
      drow = SDD_Name(Host_BeginRow)(state,i,0);
      SDD_Name(Host_BeginUpdate)(state,&drow,HD.XOffset);
      SDD_Name(Host_WritePixels)(state,&drow,col,HD.XOffset);
      SDD_Name(Host_EndUpdate)(state,&drow);
      displaywidth = HD.XScale*DC.LastHostWidth;
      SDD_Name(Host_SkipPixels)(state,&drow,displaywidth);
      rightborder = HD.Width-(displaywidth+HD.XOffset);
      SDD_Name(Host_BeginUpdate)(state,&drow,rightborder);
      SDD_Name(Host_WritePixels)(state,&drow,col,rightborder);
      SDD_Name(Host_EndUpdate)(state,&drow);
      SDD_Name(Host_EndRow)(state,&drow);
    }
  }

  /* Display area */

  flags = HD.RefreshFlags[row>>5];
  bit = UINT32_C(1)<<(row&31);
  if(flags & bit)
  {
    VIDEO_STAT(DisplayRowForce,1,1);
    rowflags = ROWFUNC_FORCE;
    HD.RefreshFlags[row>>5] = (flags &~ bit);
  }

  drow = SDD_Name(Host_BeginRow)(state,hoststart++,HD.XOffset);
  rf = &SDD_Name(RowFuncs)[HD.XScale-1][(DC.VIDC_CR&0xc)>>2];
  if(hoststart == hostend)
  {
    if((*rf)(state,row,drow,rowflags | ROWFUNC_UPDATEFLAGS))
    {
      VIDEO_STAT(DisplayRowRedraw,1,1);
    }
    SDD_Name(Host_EndRow)(state,&drow);
  }
  else
  {
    /* Remember current Vptr */
    uint32_t Vptr = DC.Vptr;
    int updated = (*rf)(state,row,drow,rowflags);
    SDD_Name(Host_EndRow)(state,&drow);
    if(updated)
    {
      VIDEO_STAT(DisplayRowRedraw,1,1);
      /* Call the same func again on the same source data to update the copies of this scanline */
      while(hoststart < hostend)
      {
        DC.Vptr = Vptr;
        drow = SDD_Name(Host_BeginRow)(state,hoststart++,HD.XOffset);
        if(hoststart == hostend)
          rowflags |= ROWFUNC_UPDATEFLAGS;
        (*rf)(state,row,drow,rowflags);
        SDD_Name(Host_EndRow)(state,&drow);
      }
    }
  }
}

static const SDD_Name(RowFuncNoFlags) SDD_Name(RowFuncsNoFlags)[2][4] = {
 { /* 1X horizontal scaling */
   SDD_Name(RowFunc1bpp1XNoFlags),
   SDD_Name(RowFunc2bpp1XNoFlags),
   SDD_Name(RowFunc4bpp1XNoFlags),
   SDD_Name(RowFunc8bpp1XNoFlags)
 },
 { /* 2X horizontal scaling */
   SDD_Name(RowFunc1bpp2XNoFlags),
   SDD_Name(RowFunc2bpp2XNoFlags),
   SDD_Name(RowFunc4bpp2XNoFlags),
   SDD_Name(RowFunc8bpp2XNoFlags)
 }
};

static void SDD_Name(DisplayRowNoFlags)(ARMul_State *state,int row)
{
  SDD_HostColour col;
  const SDD_Name(RowFuncNoFlags) *rf;
  uint32_t Vptr;
  /* Render a display row */
  int hoststart = (row-(VIDC.Vert_DisplayStart+1))*HD.YScale+HD.YOffset;
  int hostend = hoststart + HD.YScale;
  if(hoststart < 0)
    hoststart = 0;
  if(hostend > HD.Height)
    hostend = HD.Height;
  if(hoststart >= hostend)
    return;

  /* Handle border colour updates */
  col = HD.BorderCol;
  
  if(DC.ForceRefresh || (HD.BorderCols[row] != col))
  {
    int i;
    VIDEO_STAT(BorderRedraw,1,1);
    VIDEO_STAT(BorderRedrawForced,DC.ForceRefresh,1);
    VIDEO_STAT(BorderRedrawColourChanged,(HD.BorderCols[row] != col),1);
    HD.BorderCols[row] = col;
    for(i=hoststart;i<hostend;i++)
    {
      int displaywidth, rightborder;
      SDD_Row drow = SDD_Name(Host_BeginRow)(state,i,0);
      SDD_Name(Host_BeginUpdate)(state,&drow,HD.XOffset);
      SDD_Name(Host_WritePixels)(state,&drow,col,HD.XOffset);
      SDD_Name(Host_EndUpdate)(state,&drow);
      displaywidth = HD.XScale*DC.LastHostWidth;
      SDD_Name(Host_SkipPixels)(state,&drow,displaywidth);
      rightborder = HD.Width-(displaywidth+HD.XOffset);
      SDD_Name(Host_BeginUpdate)(state,&drow,rightborder);
      SDD_Name(Host_WritePixels)(state,&drow,col,rightborder);
      SDD_Name(Host_EndUpdate)(state,&drow);
      SDD_Name(Host_EndRow)(state,&drow);
    }
  }

  /* Display area */

  rf = &SDD_Name(RowFuncsNoFlags)[HD.XScale-1][(DC.VIDC_CR&0xc)>>2];
  /* Remember current Vptr */
  Vptr = DC.Vptr;
  do
  {
    SDD_Row drow;
    DC.Vptr = Vptr;
    drow = SDD_Name(Host_BeginRow)(state,hoststart,HD.XOffset);
    (*rf)(state,row,drow);
    SDD_Name(Host_EndRow)(state,&drow);
  } while(++hoststart < hostend);
}

/*

  EventQ funcs

*/

static void SDD_Name(FrameEnd)(ARMul_State *state,CycleCount nowtime); /* Trigger vsync interrupt */
static void SDD_Name(FrameStart)(ARMul_State *state,CycleCount nowtime); /* End of vsync, prepare for new frame */
static void SDD_Name(RowStart)(ARMul_State *state,CycleCount nowtime); /* Fill in a display/border row */

static void SDD_Name(Flyback)(ARMul_State *state)
{
  CycleCount oldrate = ARMul_EmuRate;

  if(DC.FLYBK)
    return;

  /* Trigger VSync */
  DC.FLYBK = true;
  DisplayDev_VSync(state);

  /* If EmuRate has just changed, recalculate the line rate now to try and keep things in sync */
  if(oldrate != ARMul_EmuRate)
  {
    static const uint_fast8_t ClockDividers[4] = {
    /* Source rates:     24.0MHz     25.0MHz      36.0MHz */
      6, /* 1/3      ->   8.0MHz      8.3MHz      12.0MHz */
      4, /* 1/2      ->  12.0MHz     12.5MHz      18.0MHz */
      3, /* 2/3      ->  16.0MHz     16.6MHz      24.0MHz */
      2, /* 1/1      ->  24.0MHz     25.0MHz      36.0MHz */
    };
  
    const uint_fast16_t NewCR = VIDC.ControlReg;
    const uint32_t ClockIn = 2*DisplayDev_GetVIDCClockIn();
    const uint_fast8_t ClockDivider = ClockDividers[NewCR&3]; 
  
    /* Calculate new line rate */
    DC.LineRate = (((uint64_t) ARMul_EmuRate)*(VIDC.Horiz_Cycle*2+2))*ClockDivider/ClockIn;
    if(DC.LineRate < 100)
      DC.LineRate = 100; /* Clamp to safe minimum value */
  }
}

static void SDD_Name(Reschedule)(ARMul_State *state,CycleCount nowtime,EventQ_Func func,int row,bool flybk)
{
  int rows, idx=0;
  /* Force frame end just in case registers have been poked mid-frame */
  if(row >= VIDC.Vert_Cycle+1)
  {
    func = SDD_Name(FrameEnd);
    flybk = true;
  }
  if(flybk)
  {
    EventQ_Func oldfunc = state->EventQ[0].Func;
    SDD_Name(Flyback)(state);
    idx = EventQ_Find2(state,oldfunc);
  }
  rows = row-DC.NextRow;
  if(rows < 1)
    rows = 1;
  DC.LastRow = DC.NextRow;
  DC.NextRow = row;
  nowtime = state->EventQ[0].Time; /* Ignore the supplied time and use the time the event was last scheduled for - should eliminate any slip/skew */
  EventQ_Reschedule(state,nowtime+rows*DC.LineRate,func,idx);
}

static void SDD_Name(DisplayEnd)(ARMul_State *state,CycleCount nowtime)
{
  /* Go to FrameEnd, with a VSync trigger */
  SDD_Name(Reschedule)(state,nowtime,SDD_Name(FrameEnd),(VIDC.Vert_Cycle+1),true);
}

static void SDD_Name(SkipFrame)(ARMul_State *state,CycleCount nowtime)
{
  /* Work out which scanline VSync is due on */
  int vsync = MAX(VIDC.Vert_DisplayStart,VIDC.Vert_DisplayEnd);
  SDD_Name(Reschedule)(state,nowtime,SDD_Name(DisplayEnd),vsync+1,false);
}

static void SDD_Name(FrameStart)(ARMul_State *state,CycleCount nowtime)
{
  bool newDMAEn;
  /* Assuming a multiplier of 2, these are the required clock dividers
     (selected via bottom two bits of VIDC.ControlReg): */
  static const uint_fast8_t ClockDividers[4] = {
  /* Source rates:     24.0MHz     25.0MHz      36.0MHz */
    6, /* 1/3      ->   8.0MHz      8.3MHz      12.0MHz */
    4, /* 1/2      ->  12.0MHz     12.5MHz      18.0MHz */
    3, /* 2/3      ->  16.0MHz     16.6MHz      24.0MHz */
    2, /* 1/1      ->  24.0MHz     25.0MHz      36.0MHz */
  };

  const uint_fast16_t NewCR = VIDC.ControlReg;
  const uint32_t ClockIn = 2*DisplayDev_GetVIDCClockIn();
  const uint_fast8_t ClockDivider = ClockDividers[NewCR&3];

  /* Calculate new line rate */
  DC.LineRate = (((uint64_t) ARMul_EmuRate)*(VIDC.Horiz_Cycle*2+2))*ClockDivider/ClockIn;
  if(DC.LineRate < 100)
    DC.LineRate = 100; /* Clamp to safe minimum value */

  DC.FLYBK = false;

  if(DisplayDev_UseUpdateFlags)
  {
    /* Handle frame skip */
    if(DC.FrameSkip--)
    {
      SDD_Name(SkipFrame)(state,nowtime);
      return;
    }
    DC.FrameSkip = DisplayDev_FrameSkip;
  }

  /* Ensure mode changes if pixel clock changed */
  DC.ModeChanged |= (DC.VIDC_CR & 3) != (NewCR & 3);

  /* Force full refresh if DMA just toggled on/off */
  newDMAEn = (MEMC.ControlReg>>10)&1;
  DC.ForceRefresh = (newDMAEn ^ DC.DMAEn);
  DC.DMAEn = newDMAEn;
  VIDEO_STAT(ForceRefreshDMA,DC.ForceRefresh,1);

  /* Ensure full palette rebuild & screen refresh on BPP change */
  if((DC.VIDC_CR & 0xc) != (NewCR & 0xc))
  {
    VIDEO_STAT(ForceRefreshBPP,1,1);
    DC.DirtyPalette = 65535;
    DC.ForceRefresh = true;
  }

  /* Vinit changes require a full refresh also */
  if(DC.LastVinit != MEMC.Vinit)
  {
    DC.LastVinit = MEMC.Vinit;
    if(!DC.ForceRefresh) /* No point setting RefreshFlags if already doing full refresh */
    {
      VIDEO_STAT(RefreshFlagsVinit,1,1);
      memset(HD.RefreshFlags,0xff,sizeof(HD.RefreshFlags));
    }
  }

  DC.VIDC_CR = NewCR;

  /* Handle any mode changes */
  if(DC.ModeChanged)
  {
    int32_t FramePeriod;
    int FrameRate;
    /* Work out new screen parameters
       TODO - Using display area isn't always appropriate, e.g. Stunt Racer 2000
       does some screen transitions by adjusting the display area height over
       time.
       Determining screen size via the borders would be better, but wouldn't
       allow the host to hide the borders and so use better display modes/scale
       factors. */
    int Width = (VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
    int Height = (VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart);
    if(Height <= 0)
    {
      /* Display output has been forced off by setting end addr before start
         Try using border size instead */
      Height = VIDC.Vert_BorderEnd-VIDC.Vert_BorderStart;
    }
    FramePeriod = (VIDC.Horiz_Cycle*2+2)*(VIDC.Vert_Cycle+1);
    FrameRate = ClockIn/(FramePeriod*ClockDivider);
    
    if((Width != DC.LastHostWidth) || (Height != DC.LastHostHeight) || (FrameRate != DC.LastHostHz))
    {
      fprintf(stderr,"New mode: %dx%d, %dHz (CR %x ClockIn %dMhz)\n",Width,Height,FrameRate,NewCR,(int)(ClockIn/2000000));
#ifdef VIDEO_STATS
      vidstats_Dump("Stats for previous mode");
#endif
      /* Try selecting new mode */
      if((Width < 1) || (Height < 1))
      {
        /* Bad mode; skip straight to FrameEnd state */
        SDD_Name(Reschedule)(state,nowtime,SDD_Name(FrameEnd),(VIDC.Vert_Cycle+1),false);
        return;
      }
      
      DC.LastHostWidth = Width;
      DC.LastHostHeight = Height;
      DC.LastHostHz = FrameRate;
      SDD_Name(Host_ChangeMode)(state,Width,Height,FrameRate);

      /* Calculate display offsets, for start of first display pixel */
      HD.XOffset = (HD.Width-Width*HD.XScale)/2;
      HD.YOffset = (HD.Height-Height*HD.YScale)/2;
      DC.ForceRefresh = true;
    }
    DC.ModeChanged = false;
  }

#ifdef VIDEO_STATS
  if(vidstats[vidstat_DisplayFrames] >= 100)
    vidstats_Dump("Stats for last 100 frames");
#endif

  /* Update AutoUpdateFlags */
  if(DisplayDev_AutoUpdateFlags)
  {
    DC.Auto_FrameCount++;
    if(DC.ForceRefresh || HD.RefreshFlags[0])
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

  /* Set up DMA */
  DC.Vptr = MEMC.Vinit<<7;

  if(DisplayDev_UseUpdateFlags)
  {
    /* Schedule for first border row */
    SDD_Name(Reschedule)(state,nowtime,SDD_Name(RowStart),VIDC.Vert_BorderStart+1,false);
  }
  else
  {
    /* Only update if forced, or frameskip has run out
       We use the first RefreshFlags entry to detect if any DMA changes have occured since the start of the last frame. If any have, we redraw the entire screen */
    if(DC.ForceRefresh || HD.RefreshFlags[0] || !DC.FrameSkip)
    {
      DC.FrameSkip = DisplayDev_FrameSkip;
      HD.RefreshFlags[0] = 0;

      /* Schedule for first border row */
      SDD_Name(Reschedule)(state,nowtime,SDD_Name(RowStart),VIDC.Vert_BorderStart+1,false);
    }
    else
    {
      DC.FrameSkip--;
      SDD_Name(SkipFrame)(state,nowtime);
      return;
    }
  }      
  
  /* Update host */
  SDD_Name(Host_PollDisplay)(state);
}

static void SDD_Name(FrameEnd)(ARMul_State *state,CycleCount nowtime)
{
  VIDEO_STAT(DisplayFrames,1,1);

  SDD_Name(Flyback)(state); /* Paranoia */

  /* Set up the next frame */
  DC.LastRow = 0;
  DC.NextRow = VIDC.Vert_SyncWidth+1;
  nowtime = state->EventQ[0].Time; /* Ignore the supplied time and use the time the event was last scheduled for - should eliminate any slip/skew */
  EventQ_Reschedule(state,nowtime+DC.NextRow*DC.LineRate,SDD_Name(FrameStart),EventQ_Find2(state,SDD_Name(FrameEnd)));
}

static void SDD_Name(RowStart)(ARMul_State *state,CycleCount nowtime)
{
  int nextrow;
  int stop = DC.NextRow;
  bool dmaen = DC.DMAEn;
  bool flybk = false;
  int row = DC.LastRow;
  if(row < VIDC.Vert_BorderStart+1)
    row = VIDC.Vert_BorderStart+1; /* Skip pre-border rows */
  while(row < stop)
  {
    if(row < (VIDC.Vert_DisplayStart+1))
    {
      /* Border region */
      SDD_Name(BorderRow)(state,row);
    }
    else if(dmaen && (row < (VIDC.Vert_DisplayEnd+1)))
    {
      /* Display */
      if(DisplayDev_UseUpdateFlags)
      {
        SDD_Name(DisplayRow)(state,row);
      }
      else
      {
        SDD_Name(DisplayRowNoFlags)(state,row);
      }
    }
    else if(row < (VIDC.Vert_BorderEnd+1))
    {
      /* Border again */
      flybk = true;
      SDD_Name(BorderRow)(state,row);
    }
    else
    {
      /* Reached end of screen */
      SDD_Name(Reschedule)(state,nowtime,SDD_Name(FrameEnd),VIDC.Vert_Cycle+1,flybk);
      return;
    }
    VIDEO_STAT(DisplayRows,1,1);
    row++;
  }
  /* If we've just drawn the last display row, it's time for a vsync */
  if((stop >= (VIDC.Vert_DisplayStart+1)) && (stop >= (VIDC.Vert_DisplayEnd+1)))
  {
    flybk = true;
  }
  /* Skip ahead to next row */
  nextrow = row+SDD_RowsAtOnce;
  if((SDD_RowsAtOnce > 1) && (row <= VIDC.Vert_Cycle) && (nextrow > VIDC.Vert_Cycle+1))
    nextrow = VIDC.Vert_Cycle+1;
  SDD_Name(Reschedule)(state,nowtime,SDD_Name(RowStart),nextrow,flybk);
}

/*

  VIDC/IOEB write handler

*/

static void SDD_Name(VIDCPutVal)(ARMul_State *state,ARMword address, ARMword data,bool bNw) {
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
      if(!(DC.DirtyPalette & (1<<Log)))
      {
        VIDEO_STAT(RefreshFlagsPalette,1,1);
        /* TODO - Make it configurable whether palette changes cause a screen refresh when UseUpdateFlags == false */
        if(!DC.DirtyPalette && DisplayDev_UseUpdateFlags)
        {
          memset(HD.RefreshFlags,0xff,sizeof(HD.RefreshFlags));
        }
        DC.DirtyPalette |= (1<<Log);
      }
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
        HD.BorderCol = SDD_Name(Host_GetColour)(state,val);
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

static void SDD_Name(IOEBCRWrite)(ARMul_State *state,ARMword data) {
  DC.ModeChanged = true;
};

/*

  DisplayDev wrapper

*/

static int SDD_Name(Init)(ARMul_State *state,const struct Vidc_Regs *Vidc)
{
  state->Display = calloc(sizeof(struct SDD_Name(DisplayInfo)),1);
  if(!state->Display) {
    fprintf(stderr,"Failed to allocate DisplayInfo\n");
    return -1;
  }

  VIDC = *Vidc;

  DC.ModeChanged = true;
  DC.LastHostWidth = DC.LastHostHeight = DC.LastHostHz = -1;
  DC.DirtyPalette = 65535;
  DC.NextRow = 0;
  DC.LastRow = 0;
  DC.MaxRow = 0;
  DC.VIDC_CR = 0;
  DC.DMAEn = false;
  DC.FLYBK = false;
  DC.LineRate = 10000;
  DC.LastVinit = MEMC.Vinit;
  HD.BorderCol = SDD_Name(Host_GetColour)(state,VIDC.BorderCol);

  memset(HOSTDISPLAY.RefreshFlags,0xff,sizeof(HOSTDISPLAY.RefreshFlags));
  memset(HOSTDISPLAY.UpdateFlags,0,sizeof(HOSTDISPLAY.UpdateFlags)); /* Initial value in MEMC.UpdateFlags is 1 */   

  /* Schedule first update event */
  EventQ_Insert(state,ARMul_Time+100,SDD_Name(FrameStart));

  return 0;
}

static void SDD_Name(Shutdown)(ARMul_State *state)
{
  int idx = EventQ_Find(state,SDD_Name(FrameStart));
  if(idx == -1)
    idx = EventQ_Find(state,SDD_Name(FrameEnd));
  if(idx == -1)
    idx = EventQ_Find(state,SDD_Name(RowStart));
  if(idx == -1)
    idx = EventQ_Find(state,SDD_Name(DisplayEnd));
  if(idx >= 0)
    EventQ_Remove(state,idx);
  else
  {
    ControlPane_Error(EXIT_FAILURE,"Couldn't find SDD event func!\n");
  }
  free(state->Display);
  state->Display = NULL;
}

static void SDD_Name(DAGWrite)(ARMul_State *state,int reg,ARMword val)
{
  /* We only care about Vstart & Vend updates. Vinit updates are picked up on at the start of the frame. */
  switch(reg)
  {
  case 1: /* Vstart */
  case 2: /* Vend */
    memset(HD.RefreshFlags,0xff,sizeof(HD.RefreshFlags));
    break;
  }
}

const DisplayDev SDD_DisplayDev = {
  SDD_Name(Init),
  SDD_Name(Shutdown),
  SDD_Name(VIDCPutVal),
  SDD_Name(DAGWrite),
  SDD_Name(IOEBCRWrite),
};

/*

  The end

*/

#undef VideoRelUpdateAndForce
#undef ROWFUNC_FORCE
#undef ROWFUNC_UPDATEFLAGS
#undef ROWFUNC_UPDATED

