/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info
   SDL version by Cameron Cawley, 2023 */
/* Display and keyboard interface for the Arc emulator */

/*#define DEBUG_VIDCREGS */
/*#define DEBUG_KBD */
/*#define DEBUG_MOUSEMOVEMENT */

#include <stdio.h>
#include <stdlib.h>

#include "platform.h"

#include "armdefs.h"
#include "arch/dbugsys.h"
#include "arch/keyboard.h"

#include "KeyTable.h"

#if SDL_VERSION_ATLEAST(2, 0, 0)
SDL_Window *window = NULL;
#endif

#if SDL_VERSION_ATLEAST(3, 0, 0)
static void ToggleGrab(void) {
  SDL_SetWindowRelativeMouseMode(window, !SDL_GetWindowRelativeMouseMode(window));
}

static bool IsGrabbed(void) {
  return SDL_GetWindowRelativeMouseMode(window);
}
#elif SDL_VERSION_ATLEAST(2, 0, 0)
static void ToggleGrab(void) {
  SDL_SetRelativeMouseMode(!SDL_GetRelativeMouseMode());
}

static bool IsGrabbed(void) {
  return SDL_GetRelativeMouseMode();
}
#else
static void ToggleGrab(void) {
  if (SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_OFF) {
    SDL_WM_GrabInput(SDL_GRAB_ON);
    SDL_ShowCursor(0);
  } else {
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(1);
  }
}

static bool IsGrabbed(void) {
  return (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_OFF);
}
#endif

/*-----------------------------------------------------------------------------*/
static void ProcessKey(ARMul_State *state, const SDL_KeyboardEvent *key, bool up) {
#if SDL_VERSION_ATLEAST(3, 0, 0)
  const SDL_Keycode sym = key->key;
#else
  const SDL_Keycode sym = key->keysym.sym;
#endif

  if (sym == SDLK_KP_PLUS && up)
      ToggleGrab();

  const sdlk_to_arch_key *ktak;
  for (ktak = sdlk_to_arch_key_map; ktak->sym; ktak++) {
    if (ktak->sym == sym) {
      keyboard_key_changed(&KBD, ktak->kid, up);
      return;
    }
  }

  warn_kbd("ProcessKey: unknown key: keysym=%u\n", sym);
} /* ProcessKey */

/*-----------------------------------------------------------------------------*/
static void ProcessMouseButton(ARMul_State *state, const SDL_MouseButtonEvent *button, bool up) {
  switch (button->button) {
  case SDL_BUTTON_LEFT:
    keyboard_key_changed(&KBD, ARCH_KEY_button_1, up);
    break;
  case SDL_BUTTON_RIGHT:
    keyboard_key_changed(&KBD, ARCH_KEY_button_3, up);
    break;
#if !SDL_VERSION_ATLEAST(2, 0, 0)
  case SDL_BUTTON_WHEELUP:
    keyboard_key_changed(&KBD, ARCH_KEY_button_4, up);
    break;
  case SDL_BUTTON_WHEELDOWN:
    keyboard_key_changed(&KBD, ARCH_KEY_button_5, up);
    break;
#endif
  case SDL_BUTTON_MIDDLE:
  default:
    keyboard_key_changed(&KBD, ARCH_KEY_button_2, up);
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

  if (!IsGrabbed())
    return;

  xdiff =  (motion->xrel);
  ydiff = -(motion->yrel);

#ifdef DEBUG_MOUSEMOVEMENT
  dbug_kbd("MouseMoved: xdiff = %d  ydiff = %d\n",
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
    case SDL_EVENT_QUIT:
      ARMul_Exit(state, 0);
      break;
    case SDL_EVENT_KEY_DOWN:
      ProcessKey(state, &event.key, false);
      break;
    case SDL_EVENT_KEY_UP:
      ProcessKey(state, &event.key, true);
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      ProcessMouseButton(state, &event.button, false);
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      ProcessMouseButton(state, &event.button, true);
      break;
    case SDL_EVENT_MOUSE_MOTION:
      ProcessMouseMotion(state, &event.motion);
      break;
#if SDL_VERSION_ATLEAST(2, 0, 0)
    case SDL_EVENT_MOUSE_WHEEL:
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
