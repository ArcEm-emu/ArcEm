/*
  SDL/render.c

  (c) 2023 Cameron Cawley

  Standard display device implementation for accelerated SDL displays

*/

#include "platform.h"

#if SDL_VERSION_ATLEAST(2, 0, 0)

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

static SDL_Renderer *renderer = NULL;
#if SDL_VERSION_ATLEAST(3, 0, 0)
static const SDL_PixelFormatDetails *format = NULL;
#else
static SDL_PixelFormat *format = NULL;
#endif
static SDL_Surface *sdd_surface = NULL;
static SDL_Texture *sdd_texture = NULL;
static SDL_Surface *mouse_surface = NULL;
static SDL_Texture *mouse_texture = NULL;
static SDL_FRect mouse_rect;
static int xscale = 1, yscale = 1;

static uint32_t GetColour(ARMul_State *state,unsigned int col);
static void SetupScreen(ARMul_State *state,int width,int height,int hz);
static void PollDisplay(ARMul_State *state);

/* ------------------------------------------------------------------ */

/* Standard display device, 16bpp */

#define SDD_HostColour uint16_t
#define SDD_Name(x) sdd16_##x
#define SDD_RowsAtOnce 1
#define SDD_Row SDD_HostColour *
#define SDD_DisplayDev SDD16R_DisplayDev

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

static void SDD_Name(Host_PollDisplay)(ARMul_State *state) { PollDisplay(state); }

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  if (width > MaxVideoWidth || height > MaxVideoHeight) {
    ControlPane_Error(true,"Resize_Window: new size (%d, %d) exceeds maximum (%d, %d)",
        width, height, MaxVideoWidth, MaxVideoHeight);
  }

  HD.XScale = 1;
  HD.YScale = 1;
  HD.Width = width;
  HD.Height = height;

  SetupScreen(state,width,height,hz);
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
#define SDD_DisplayDev SDD32R_DisplayDev

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

static void SDD_Name(Host_PollDisplay)(ARMul_State *state) { PollDisplay(state); }

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  if (width > MaxVideoWidth || height > MaxVideoHeight) {
    ControlPane_Error(true,"Resize_Window: new size (%d, %d) exceeds maximum (%d, %d)",
        width, height, MaxVideoWidth, MaxVideoHeight);
  }

  HD.XScale = 1;
  HD.YScale = 1;
  HD.Width = width;
  HD.Height = height;

  SetupScreen(state,width,height,hz);
}

#undef SDD_HostColour
#undef SDD_Name
#undef SDD_RowsAtOnce
#undef SDD_Row
#undef SDD_DisplayDev

/* ------------------------------------------------------------------ */

static uint32_t GetColour(ARMul_State *state,unsigned int col)
{
  uint8_t r, g, b;

  /* Convert to 8-bit component values */
  r = (col & 0xf)*0x11;
  g = ((col>>4) & 0xf)*0x11;
  b = ((col>>8) & 0xf)*0x11;

#if SDL_VERSION_ATLEAST(3, 0, 0)
  return SDL_MapRGB(format, NULL, r, g, b);
#else
  return SDL_MapRGB(format, r, g, b);
#endif
}

/* Refresh the mouse's image                                                    */
static void RefreshMouse(ARMul_State *state) {
#if SDL_VERSION_ATLEAST(3, 0, 0)
  SDL_Palette *palette;
#endif
  int x,y,offset;
  int memptr;
  int Height = ((int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart);
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
      mouse_surface = SDL_CreateSurface(32, Height, SDL_PIXELFORMAT_INDEX8);
#endif
  mouse_rect.x = 0;
  mouse_rect.y = 0;
  mouse_rect.w = 32 * xscale;
  mouse_rect.h = Height * yscale;

  /* Cursor palette */
  for(x=0; x<3; x++) {
    uint_fast16_t phys = VIDC.CursorPalette[x];
    cursorPal[x].r = (phys & 0xf)*0x11;
    cursorPal[x].g = ((phys>>4) & 0xf)*0x11;
    cursorPal[x].b = ((phys>>8) & 0xf)*0x11;
    cursorPal[x].a = SDL_ALPHA_OPAQUE;
  }
#if SDL_VERSION_ATLEAST(3, 0, 0)
  SDL_SetPaletteColors(palette, cursorPal, 1, 3);
#else
  SDL_SetPaletteColors(mouse_surface->format->palette, cursorPal, 1, 3);
#endif
  SDL_SetSurfaceColorKey(mouse_surface, true, 0);

  SDL_LockSurface(mouse_surface);

  offset=0;
  memptr=MEMC.Cinit*16;
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
    memptr += 8;
    offset += 8;
  }; /* y */

  SDL_UnlockSurface(mouse_surface);

  if (mouse_texture)
    SDL_DestroyTexture(mouse_texture);
  mouse_texture = SDL_CreateTextureFromSurface(renderer, mouse_surface);
} /* RefreshMouse */

static void SetupScreen(ARMul_State *state,int width,int height,int hz)
{
  /* TODO: Use SDL_LockTexture() instead of creating a separate surface? */
  if (!sdd_surface)
    SDL_DestroySurface(sdd_surface);
  sdd_surface = SDL_CreateSurface(width, height, format->format);

  if (!sdd_texture)
    SDL_DestroyTexture(sdd_texture);
  sdd_texture = SDL_CreateTexture(renderer, format->format, SDL_TEXTUREACCESS_STREAMING, width, height);

  /* Try and detect rectangular pixel modes */
  if(CONFIG.bAspectRatioCorrection && (width >= height*2) && (height*2 <= MaxVideoHeight))
  {
    xscale = 1;
    yscale = 2;
    height *= 2;
  }
  else if(CONFIG.bAspectRatioCorrection && (height >= width) && (width*2 <= MaxVideoWidth))
  {
    xscale = 2;
    yscale = 1;
    width *= 2;
  }
  /* Try and detect small screen resolutions */
  else if(CONFIG.bUpscale && (width < MinVideoWidth) && (width * 2 <= MaxVideoWidth) && (height * 2 <= MaxVideoHeight))
  {
    xscale = 2;
    yscale = 2;
    width *= 2;
    height *= 2;
  }
  else
  {
    xscale = 1;
    yscale = 1;
  }

  SDL_SetWindowSize(window, width, height);
#if SDL_VERSION_ATLEAST(3, 0, 0)
  SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
#else
  SDL_RenderSetLogicalSize(renderer, width, height);
#endif
}

static void PollDisplay(ARMul_State *state)
{
  int x, y;

  SDL_UpdateTexture(sdd_texture, NULL, sdd_surface->pixels, sdd_surface->pitch);
  RefreshMouse(state);

  DisplayDev_GetCursorPos(state,&x,&y);
  mouse_rect.x = x * xscale;
  mouse_rect.y = y * yscale;

  SDL_RenderClear(renderer);
  SDL_RenderTexture(renderer, sdd_texture, NULL, NULL);
  SDL_RenderTexture(renderer, mouse_texture, NULL, &mouse_rect);
  SDL_RenderPresent(renderer);
}

static Uint32 ExaminePixelFormat(Uint32 fmt, Uint32 last)
{
  /* Reject unsupported pixel formats */
  if ((SDL_BYTESPERPIXEL(fmt) != 2 && SDL_BYTESPERPIXEL(fmt) != 4) ||
      !SDL_ISPIXELFORMAT_PACKED(fmt))
    return last;

  /* Prefer 16-bit pixel formats where possible */
  if (SDL_BYTESPERPIXEL(fmt) >= SDL_BYTESPERPIXEL(last) &&
      last != SDL_PIXELFORMAT_UNKNOWN)
    return last;

  return fmt;
}

/*-----------------------------------------------------------------------------*/
bool DisplayDev_Init(ARMul_State *state)
{
  Uint32 i, pf = SDL_PIXELFORMAT_UNKNOWN;
#if SDL_VERSION_ATLEAST(3, 0, 0)
  const SDL_PixelFormat *texture_formats;
#else
  SDL_RendererInfo info;
#endif

  /* Setup display and cursor bitmaps */
#if SDL_VERSION_ATLEAST(3, 0, 0)
  window = SDL_CreateWindow("ArcEm", 640, 512, SDL_WINDOW_RESIZABLE);
#else
  window = SDL_CreateWindow("ArcEm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            640, 512, SDL_WINDOW_RESIZABLE);
#endif
  if (!window) {
    ControlPane_Error(false,"Failed to create initial window: %s", SDL_GetError());
    return false;
  }

#if SDL_VERSION_ATLEAST(3, 0, 0)
  renderer = SDL_CreateRenderer(window, NULL);
#else
  renderer = SDL_CreateRenderer(window, -1, 0);
#endif
  if (!renderer) {
    ControlPane_Error(false,"Failed to create renderer: %s", SDL_GetError());
    return false;
  }

#if SDL_VERSION_ATLEAST(3, 0, 0)
  texture_formats = (const SDL_PixelFormat *)SDL_GetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER, NULL);
  if (texture_formats) {
    for (i = 0; i < texture_formats[i]; ++i) {
      pf = ExaminePixelFormat(texture_formats[i], pf);
    }
  }
#else
  if (SDL_GetRendererInfo(renderer, &info) >= 0) {
    for (i = 0; i < info.num_texture_formats; i++) {
      pf = ExaminePixelFormat(info.texture_formats[i], pf);
    }
  }
#endif
  if (pf == SDL_PIXELFORMAT_UNKNOWN)
    pf = SDL_PIXELFORMAT_RGBA32;

#if SDL_VERSION_ATLEAST(3, 0, 0)
  format = SDL_GetPixelFormatDetails(pf);
#else
  format = SDL_AllocFormat(pf);
#endif
  if (!format) {
    ControlPane_Error(false,"Failed to create pixel format: %s", SDL_GetError());
    return false;
  } else if (SDL_BYTESPERPIXEL(pf) == 4) {
    return DisplayDev_Set(state,&SDD32R_DisplayDev);
  } else if (SDL_BYTESPERPIXEL(pf) == 2) {
    return DisplayDev_Set(state,&SDD16R_DisplayDev);
  } else {
    ControlPane_Error(false,"Unsupported bytes per pixel: %d", SDL_BYTESPERPIXEL(pf));
    return false;
  }
}

#endif
