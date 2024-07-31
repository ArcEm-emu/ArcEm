/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info
   SDL version by Cameron Cawley, 2023 */
/* Display and keyboard interface for the Arc emulator */

/*#define DEBUG_VIDCREGS */
/*#define DEBUG_KBD */
/*#define DEBUG_MOUSEMOVEMENT */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <SDL.h>

#include "armdefs.h"
#include "arch/keyboard.h"

#include "KeyTable.h"

static void ToggleGrab(void) {
#if SDL_VERSION_ATLEAST(2, 0, 0)
  SDL_SetRelativeMouseMode(!SDL_GetRelativeMouseMode());
#else
  if (SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_OFF) {
    SDL_WM_GrabInput(SDL_GRAB_ON);
    SDL_ShowCursor(0);
  } else {
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(1);
  }
#endif
}

/*-----------------------------------------------------------------------------*/
static void ProcessKey(ARMul_State *state, const SDL_KeyboardEvent *key) {
  if (key->keysym.sym == SDLK_KP_PLUS && key->state != SDL_PRESSED)
      ToggleGrab();

  const sdlk_to_arch_key *ktak;
  for (ktak = sdlk_to_arch_key_map; ktak->sym; ktak++) {
    if (ktak->sym == key->keysym.sym) {
      keyboard_key_changed(&KBD, ktak->kid, key->state != SDL_PRESSED);
      return;
    }
  }

  fprintf(stderr, "ProcessKey: unknown key: keysym=%u\n", key->keysym.sym);
} /* ProcessKey */

/*-----------------------------------------------------------------------------*/
static void ProcessMouseButton(ARMul_State *state, const SDL_MouseButtonEvent *button) {
  switch (button->button) {
  case SDL_BUTTON_LEFT:
    keyboard_key_changed(&KBD, ARCH_KEY_button_1, button->state != SDL_PRESSED);
    break;
  case SDL_BUTTON_RIGHT:
    keyboard_key_changed(&KBD, ARCH_KEY_button_3, button->state != SDL_PRESSED);
    break;
#if !SDL_VERSION_ATLEAST(2, 0, 0)
  case SDL_BUTTON_WHEELUP:
    keyboard_key_changed(&KBD, ARCH_KEY_button_4, button->state != SDL_PRESSED);
    break;
  case SDL_BUTTON_WHEELDOWN:
    keyboard_key_changed(&KBD, ARCH_KEY_button_5, button->state != SDL_PRESSED);
    break;
#endif
  case SDL_BUTTON_MIDDLE:
  default:
    keyboard_key_changed(&KBD, ARCH_KEY_button_2, button->state != SDL_PRESSED);
    break;
  }
} /* ProcessMouseButton */

/*-----------------------------------------------------------------------------*/
#if SDL_VERSION_ATLEAST(2, 0, 0)
static void ProcessMouseWheel(ARMul_State *state, const SDL_MouseWheelEvent *wheel) {
  if (wheel->y > 0) {
    keyboard_key_changed(&KBD, ARCH_KEY_button_4, 1);
  } else if (wheel->y < 0) {
    keyboard_key_changed(&KBD, ARCH_KEY_button_5, 1);
  }
} /* ProcessMouseWheel */
#endif

static void ProcessMouseMotion(ARMul_State *state, const SDL_MouseMotionEvent *motion) {
  int xdiff, ydiff;

#if SDL_VERSION_ATLEAST(2, 0, 0)
  if (!SDL_GetRelativeMouseMode())
    return;
#else
  if (SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_OFF)
    return;
#endif

  xdiff =  (motion->xrel);
  ydiff = -(motion->yrel);

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: xdiff = %d  ydiff = %d\n",
          xdiff, ydiff);
#endif

  if (xdiff > 63)
    xdiff=63;
  if (xdiff < -63)
    xdiff=-63;

  if (ydiff>63)
    ydiff=63;
  if (ydiff<-63)
    ydiff=-63;

  KBD.MouseXCount = xdiff & 127;
  KBD.MouseYCount = ydiff & 127;

}

/*-----------------------------------------------------------------------------*/
int
Kbd_PollHostKbd(ARMul_State *state)
{
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      exit(0);
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      ProcessKey(state, &event.key);
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      ProcessMouseButton(state, &event.button);
      break;
    case SDL_MOUSEMOTION:
      ProcessMouseMotion(state, &event.motion);
      break;
#if SDL_VERSION_ATLEAST(2, 0, 0)
    case SDL_MOUSEWHEEL:
      ProcessMouseWheel(state, &event.wheel);
      break;
#endif
    }
  }
  return 0;
}

/*-----------------------------------------------------------------------------*/
int fakemain(int argc,char *argv[]);

int main(int argc, char *argv[])
{
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

  return fakemain(argc, argv);
}