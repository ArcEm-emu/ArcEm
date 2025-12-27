/*
  SDL/fb.c

  (c) 2023 Cameron Cawley

  Standard display device implementation for framebuffer SDL displays

*/

#include "platform.h"

/* TODO: Allow selecting the display device at runtime? */
#if !SDL_VERSION_ATLEAST(2, 0, 0)

#include <stdlib.h>

#include "../armdefs.h"
#include "../arch/ArcemConfig.h"
#include "../arch/armarc.h"
#include "../arch/archio.h"
#include "../eventq.h"
#include "../arch/dbugsys.h"
#include "../arch/displaydev.h"
#include "../arch/ControlPane.h"
#include <stdlib.h>

/* An upper limit on how big to support monitor size, used for
   allocating a scanline buffer and bounds checking. It's much
   more than a VIDC1 can handle, and should be pushing the RPC/A7000
   VIDC too, if we ever get around to supporting that. */
#define MinVideoWidth 512
#define MaxVideoWidth 2048
#define MaxVideoHeight 1536

static SDL_Surface *screen = NULL;
static SDL_Surface *sdd_surface = NULL;
static SDL_Surface *mouse_surface = NULL;
static SDL_Rect mouse_rect;

static uint32_t GetColour(ARMul_State *state,unsigned int col);
static void SetupScreen(ARMul_State *state,int *width,int *height,int hz,int bpp);
static void PollDisplay(ARMul_State *state,int XScale,int YScale);

/* ------------------------------------------------------------------ */

/* Standard display device, 16bpp */

#define SDD_HostColour uint16_t
#define SDD_Name(x) sdd16_##x
#define SDD_RowsAtOnce 1
#define SDD_Row SDD_HostColour *
#define SDD_DisplayDev SDD16_DisplayDev

static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col) { return GetColour(state, col); }

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  return ((SDD_Row)(void *) ((uint8_t *)sdd_surface->pixels + sdd_surface->pitch*row))+offset;
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ }

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count) { /* nothing */ }

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ }

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { (*row) += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix) { *(*row)++ = pix; }

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count) { while(count--) *(*row)++ = pix; }

static void SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  if (width > MaxVideoWidth || height > MaxVideoHeight) {
      ControlPane_Error(true,"Resize_Window: new size (%d, %d) exceeds maximum (%d, %d)",
          width, height, MaxVideoWidth, MaxVideoHeight);
  }

  HD.XScale = 1;
  HD.YScale = 1;
  /* Try and detect rectangular pixel modes */
  if(CONFIG.bAspectRatioCorrection && (width >= height*2) && (height*2 <= MaxVideoHeight))
  {
    HD.YScale = 2;
    height *= 2;
  }
  else if(CONFIG.bAspectRatioCorrection && (height >= width) && (width*2 <= MaxVideoWidth))
  {
    HD.XScale = 2;
    width *= 2;
  }
  /* Try and detect small screen resolutions */
  else if(CONFIG.bUpscale && (width < MinVideoWidth) && (width * 2 <= MaxVideoWidth) && (height * 2 <= MaxVideoHeight))
  {
    HD.XScale = 2;
    HD.YScale = 2;
    width *= 2;
    height *= 2;
  }
  HD.Width = width;
  HD.Height = height;

  SetupScreen(state,&HD.Width,&HD.Height,hz,sizeof(SDD_HostColour)*8);
}

static void SDD_Name(Host_PollDisplay)(ARMul_State *state) {
  int HorizPos, VertPos;
  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  mouse_rect.x = HorizPos*HD.XScale+HD.XOffset;
  mouse_rect.y = VertPos*HD.YScale+HD.YOffset;

  PollDisplay(state,HD.XScale,HD.YScale);
}

#undef SDD_HostColour
#undef SDD_Name
#undef SDD_RowsAtOnce
#undef SDD_Row
#undef SDD_DisplayDev

/* Standard display device, 32bpp */

#define SDD_HostColour uint32_t
#define SDD_Name(x) sdd32_##x
#define SDD_RowsAtOnce 1
#define SDD_Row SDD_HostColour *
#define SDD_DisplayDev SDD32_DisplayDev

static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col) { return GetColour(state, col); }

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  return ((SDD_Row)(void *) ((uint8_t *)sdd_surface->pixels + sdd_surface->pitch*row))+offset;
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ }

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count) { /* nothing */ }

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ }

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { (*row) += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix) { *(*row)++ = pix; }

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count) { while(count--) *(*row)++ = pix; }

static void SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  if (width > MaxVideoWidth || height > MaxVideoHeight) {
      ControlPane_Error(true,"Resize_Window: new size (%d, %d) exceeds maximum (%d, %d)",
          width, height, MaxVideoWidth, MaxVideoHeight);
  }

  HD.XScale = 1;
  HD.YScale = 1;
  /* Try and detect rectangular pixel modes */
  if(CONFIG.bAspectRatioCorrection && (width >= height*2) && (height*2 <= MaxVideoHeight))
  {
    HD.YScale = 2;
    height *= 2;
  }
  else if(CONFIG.bAspectRatioCorrection && (height >= width) && (width*2 <= MaxVideoWidth))
  {
    HD.XScale = 2;
    width *= 2;
  }
  /* Try and detect small screen resolutions */
  else if(CONFIG.bUpscale && (width < MinVideoWidth) && (width * 2 <= MaxVideoWidth) && (height * 2 <= MaxVideoHeight))
  {
    HD.XScale = 2;
    HD.YScale = 2;
    width *= 2;
    height *= 2;
  }
  HD.Width = width;
  HD.Height = height;

  SetupScreen(state,&HD.Width,&HD.Height,hz,sizeof(SDD_HostColour)*8);
}

static void SDD_Name(Host_PollDisplay)(ARMul_State *state) {
  int HorizPos, VertPos;
  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  mouse_rect.x = HorizPos*HD.XScale+HD.XOffset;
  mouse_rect.y = VertPos*HD.YScale+HD.YOffset;

  PollDisplay(state,HD.XScale,HD.YScale);
}

#undef SDD_HostColour
#undef SDD_Name
#undef SDD_RowsAtOnce
#undef SDD_Row
#undef SDD_DisplayDev

/* ------------------------------------------------------------------ */

#if !SDL_VERSION_ATLEAST(2, 0, 0)

/* Palettised display code */
#define PDD_Name(x) pdd_##x

typedef struct {
  ARMword *data;
  int offset;
} PDD_Row;

static void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz);

static void PDD_Name(Host_SetPaletteEntry)(ARMul_State *state,int i,uint_fast16_t phys)
{
  SDL_Color col;

  /* Convert to 8-bit component values */
  col.r = (phys & 0xf)*0x11;
  col.g = ((phys>>4) & 0xf)*0x11;
  col.b = ((phys>>8) & 0xf)*0x11;

  SDL_SetColors(screen, &col, i, 1);
  SDL_SetColors(sdd_surface, &col, i, 1);
}

static void PDD_Name(Host_SetCursorPaletteEntry)(ARMul_State *state,int i,uint_fast16_t phys)
{
  /* TODO */
}

static void PDD_Name(Host_SetBorderColour)(ARMul_State *state,uint_fast16_t phys)
{
  /* TODO */
}

static inline PDD_Row PDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset,int *alignment)
{
  PDD_Row drow;
  uintptr_t base = ((uintptr_t)sdd_surface->pixels + sdd_surface->pitch*row) + offset;
  drow.offset = ((base<<3) & 0x18); /* Just in case bytes per line isn't aligned */
  drow.data = (ARMword *) (base & ~0x3);
  *alignment = drow.offset;
  return drow;
}

static inline void PDD_Name(Host_EndRow)(ARMul_State *state,PDD_Row *row) { /* nothing */ }

static inline ARMword *PDD_Name(Host_BeginUpdate)(ARMul_State *state,PDD_Row *row,unsigned int count,int *outoffset)
{
  *outoffset = row->offset;
  return row->data;
}

static inline void PDD_Name(Host_EndUpdate)(ARMul_State *state,PDD_Row *row) { /* nothing */ }

static inline void PDD_Name(Host_AdvanceRow)(ARMul_State *state,PDD_Row *row,unsigned int count) {
  row->offset += count;
  row->data += count>>5;
  row->offset &= 0x1f;
}

static void PDD_Name(Host_PollDisplay)(ARMul_State *state);

static void PDD_Name(Host_DrawBorderRect)(ARMul_State *state,int x,int y,int width,int height)
{
  /* TODO */
}

#include "../arch/paldisplaydev.c"

void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz)
{
  if (width > MaxVideoWidth || height > MaxVideoHeight) {
      ControlPane_Error(true,"Resize_Window: new size (%d, %d) exceeds maximum (%d, %d)",
          width, height, MaxVideoWidth, MaxVideoHeight);
  }

  HD.XScale = 1;
  HD.YScale = 1;
  /* Try and detect rectangular pixel modes */
  if(CONFIG.bAspectRatioCorrection && (width >= height*2) && (height*2 <= MaxVideoHeight))
  {
    HD.YScale = 2;
    height *= 2;
  }
  else if(CONFIG.bAspectRatioCorrection && (height >= width) && (width*2 <= MaxVideoWidth))
  {
    HD.XScale = 2;
    width *= 2;
  }
  /* Try and detect small screen resolutions */
  else if(CONFIG.bUpscale && (width < MinVideoWidth) && (width * 2 <= MaxVideoWidth) && (height * 2 <= MaxVideoHeight))
  {
    HD.XScale = 2;
    HD.YScale = 2;
    width *= 2;
    height *= 2;
  }
  HD.Width = width;
  HD.Height = height;

  SetupScreen(state,&HD.Width,&HD.Height,hz,8);

  /* Calculate expansion params */
  if((depth == 3) && (HD.XScale == 1))
  {
    /* No expansion */
    HD.ExpandTable = NULL;
  }
  else
  {
    /* Expansion! */
    static ARMword expandtable[256];
    unsigned int mul = 1;
    int i;
    HD.ExpandFactor = 0;
    while((1<<HD.ExpandFactor) < HD.XScale)
      HD.ExpandFactor++;
    HD.ExpandFactor += (3-depth);
    HD.ExpandTable = expandtable;
    for(i=0;i<HD.XScale;i++)
    {
      mul |= 1<<(i*8);
    }
    GenExpandTable(HD.ExpandTable,1<<depth,HD.ExpandFactor,mul);
  }
}

static void PDD_Name(Host_PollDisplay)(ARMul_State *state) {
  int HorizPos, VertPos;
  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  mouse_rect.x = HorizPos*HD.XScale+HD.XOffset;
  mouse_rect.y = VertPos*HD.YScale+HD.YOffset;

  PollDisplay(state,HD.XScale,HD.YScale);
}

#endif

/* ------------------------------------------------------------------ */

static uint32_t GetColour(ARMul_State *state,unsigned int col)
{
  uint8_t r, g, b;

  /* Convert to 8-bit component values */
  r = (col & 0xf)*0x11;
  g = ((col>>4) & 0xf)*0x11;
  b = ((col>>8) & 0xf)*0x11;

#if SDL_VERSION_ATLEAST(3, 0, 0)
  return SDL_MapRGB(SDL_GetPixelFormatDetails(screen->format), NULL, r, g, b);
#else
  return SDL_MapRGB(screen->format, r, g, b);
#endif
}

#undef HD

/* Refresh the mouse's image                                                    */
static void RefreshMouse(ARMul_State *state,int XScale,int YScale) {
#if SDL_VERSION_ATLEAST(3, 0, 0)
  SDL_Palette *palette;
#endif
  int x,y,offset, repeat;
  int memptr;
  int Height = ((int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart)*YScale;
  SDL_Color cursorPal[3];
  uint8_t *dst;

  if (Height < 0) Height = 0;

  if (mouse_surface && mouse_surface->h != Height)
      SDL_DestroySurface(mouse_surface), mouse_surface = NULL;
#if SDL_VERSION_ATLEAST(3, 0, 0)
  if (!mouse_surface) {
      mouse_surface = SDL_CreateSurface(32, Height, SDL_PIXELFORMAT_INDEX8);
      palette = SDL_CreateSurfacePalette(mouse_surface);
  } else {
      palette = SDL_GetSurfacePalette(mouse_surface);
  }
#else
  if (!mouse_surface)
      mouse_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, 32, Height, 8, 0, 0, 0, 0);
#endif
  mouse_rect.w = 32;
  mouse_rect.h = Height;

  /* Cursor palette */
  for(x=0; x<3; x++) {
    uint_fast16_t phys = VIDC.CursorPalette[x];
    cursorPal[x].r = (phys & 0xf)*0x11;
    cursorPal[x].g = ((phys>>4) & 0xf)*0x11;
    cursorPal[x].b = ((phys>>8) & 0xf)*0x11;
#if SDL_VERSION_ATLEAST(2, 0, 0)
    cursorPal[x].a = SDL_ALPHA_OPAQUE;
#endif
  }
#if SDL_VERSION_ATLEAST(3, 0, 0)
  SDL_SetPaletteColors(palette, cursorPal, 1, 3);
#elif SDL_VERSION_ATLEAST(2, 0, 0)
  SDL_SetPaletteColors(mouse_surface->format->palette, cursorPal, 1, 3);
#else
  SDL_SetColors(mouse_surface, cursorPal, 1, 3);
#endif
  SDL_SetSurfaceColorKey(mouse_surface, true, 0);

  SDL_LockSurface(mouse_surface);

  offset=0;
  memptr=MEMC.Cinit*16;
  repeat=0;
  dst=mouse_surface->pixels;
  for(y=0;y<Height;y++) {
    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++) {
        dst[x] = ((tmp[x/16]>>((x & 15)*2)) & 3);
      }; /* x */
      dst += mouse_surface->pitch;
    } else return;
    if(++repeat == YScale) {
      memptr += 8;
      offset += 8;
      repeat  = 0;
    }
  }; /* y */

  SDL_UnlockSurface(mouse_surface);
} /* RefreshMouse */

static void SetupScreen(ARMul_State *state,int *width,int *height,int hz,int bpp)
{
  if (!sdd_surface)
      SDL_DestroySurface(sdd_surface);
#if SDL_VERSION_ATLEAST(2, 0, 0)
  SDL_SetWindowSize(window, *width, *height);
  screen = SDL_GetWindowSurface(window);
#else
  screen = SDL_SetVideoMode(*width, *height, screen->format->BitsPerPixel,
                            SDL_SWSURFACE | SDL_HWPALETTE);
#endif
#if SDL_VERSION_ATLEAST(3, 0, 0)
  sdd_surface = SDL_CreateSurface(screen->w, screen->h, screen->format);
#else
  sdd_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, screen->w, screen->h,
                                     screen->format->BitsPerPixel,
                                     screen->format->Rmask,
                                     screen->format->Gmask,
                                     screen->format->Bmask,
                                     screen->format->Amask);
#endif
  *width = screen->w;
  *height = screen->h;
}

static void PollDisplay(ARMul_State *state,int XScale,int YScale)
{
  RefreshMouse(state,XScale,YScale);

  SDL_BlitSurface(sdd_surface, NULL, screen, NULL);
  SDL_BlitSurface(mouse_surface, NULL, screen, &mouse_rect);

#if SDL_VERSION_ATLEAST(2, 0, 0)
  SDL_UpdateWindowSurface(window);
#else
  SDL_Flip(screen);
#endif
}

/*-----------------------------------------------------------------------------*/
bool DisplayDev_Init(ARMul_State *state)
{
  int bpp;

  /* Setup display and cursor bitmaps */
#if SDL_VERSION_ATLEAST(3, 0, 0)
  window = SDL_CreateWindow("ArcEm", 640, 512, 0);
  screen = SDL_GetWindowSurface(window);
  bpp = SDL_BYTESPERPIXEL(screen->format);
#elif SDL_VERSION_ATLEAST(2, 0, 0)
  window = SDL_CreateWindow("ArcEm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            640, 512, 0);
  screen = SDL_GetWindowSurface(window);
  bpp = screen->format->BytesPerPixel;
#else
  screen = SDL_SetVideoMode(640, 512, 0, SDL_SWSURFACE);
  SDL_WM_SetCaption("ArcEm", "ArcEm");
  bpp = screen->format->BytesPerPixel;
#endif

  if (!screen) {
      ControlPane_Error(false,"Failed to create initial window: %s", SDL_GetError());
      return false;
  } else if (bpp == 4) {
      return DisplayDev_Set(state,&SDD32_DisplayDev);
  } else if (bpp == 2) {
      return DisplayDev_Set(state,&SDD16_DisplayDev);
#if !SDL_VERSION_ATLEAST(2, 0, 0)
  } else if (bpp == 1) {
      return DisplayDev_Set(state,&PDD_DisplayDev);
#endif
  } else {
      ControlPane_Error(false,"Unsupported bytes per pixel: %d", bpp);
      return false;
  }
}

#endif
