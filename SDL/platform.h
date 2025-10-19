/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info
   SDL version by Cameron Cawley, 2023 */

#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include "../c99.h"

#ifdef SYSTEM_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#if SDL_VERSION_ATLEAST(2, 0, 0)
extern SDL_Window *window;
#endif

#if !SDL_VERSION_ATLEAST(3, 0, 0)
#if SDL_VERSION_ATLEAST(2, 0, 0)
#define SDL_RenderTexture SDL_RenderCopyF
#define SDL_CreateSurface(w, h, f) SDL_CreateRGBSurfaceWithFormat(0, w, h, SDL_BITSPERPIXEL(f), f)

static inline bool SDL_SetSurfaceColorKey(SDL_Surface *surface, bool enabled, Uint32 key) {
  return (SDL_SetColorKey(surface, enabled ? SDL_TRUE : 0, key) == 0);
}
#else
static inline bool SDL_SetSurfaceColorKey(SDL_Surface *surface, bool enabled, Uint32 key) {
  return (SDL_SetColorKey(surface, enabled ? SDL_SRCCOLORKEY : 0, key) == 0);
}
#endif

#define SDL_DestroySurface SDL_FreeSurface

/* Audio formats that were renamed in SDL3 */
#define SDL_AUDIO_S16 AUDIO_S16SYS

/* Events that were renamed in SDL3 */
#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_KEY_UP SDL_KEYUP
#define SDL_EVENT_MOUSE_MOTION SDL_MOUSEMOTION
#define SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_WHEEL SDL_MOUSEWHEEL

/* Keycodes that were renamed in SDL3 */
#define SDLK_APOSTROPHE SDLK_QUOTE
#define SDLK_GRAVE SDLK_BACKQUOTE
#define SDLK_Q SDLK_q
#define SDLK_W SDLK_w
#define SDLK_E SDLK_e
#define SDLK_R SDLK_r
#define SDLK_T SDLK_t
#define SDLK_Y SDLK_y
#define SDLK_U SDLK_u
#define SDLK_I SDLK_i
#define SDLK_O SDLK_o
#define SDLK_P SDLK_p
#define SDLK_A SDLK_a
#define SDLK_S SDLK_s
#define SDLK_D SDLK_d
#define SDLK_F SDLK_f
#define SDLK_G SDLK_g
#define SDLK_H SDLK_h
#define SDLK_J SDLK_j
#define SDLK_K SDLK_k
#define SDLK_L SDLK_l
#define SDLK_Z SDLK_z
#define SDLK_X SDLK_x
#define SDLK_C SDLK_c
#define SDLK_V SDLK_v
#define SDLK_B SDLK_b
#define SDLK_N SDLK_n
#define SDLK_M SDLK_m
#endif

#if !SDL_VERSION_ATLEAST(2, 0, 0)
/* Keycodes that were renamed in SDL2 */
#define SDLK_NUMLOCKCLEAR SDLK_NUMLOCK
#define SDLK_SCROLLLOCK SDLK_SCROLLOCK
#define SDLK_PRINTSCREEN SDLK_PRINT
#define SDLK_KP_1 SDLK_KP1
#define SDLK_KP_2 SDLK_KP2
#define SDLK_KP_3 SDLK_KP3
#define SDLK_KP_4 SDLK_KP4
#define SDLK_KP_5 SDLK_KP5
#define SDLK_KP_6 SDLK_KP6
#define SDLK_KP_7 SDLK_KP7
#define SDLK_KP_8 SDLK_KP8
#define SDLK_KP_9 SDLK_KP9
#define SDLK_KP_0 SDLK_KP0

typedef SDLKey SDL_Keycode;
#endif

#endif
