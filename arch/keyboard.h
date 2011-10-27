#ifndef ARCH_KEYBOARD_H
#define ARCH_KEYBOARD_H

/* arch/keyboard.c -- a model of the Archimedes keyboard. */

/* Keys on an Acorn R140 keyboard organised into groups as you'd find
 * them on a keyboard, not by their row, column position.  You're only
 * meant to use the identifiers in the following enum, e.g.
 * ARCH_KEY_tab. */

#define ARCH_KEYBOARD_DEFINITION \
    X(escape, 0, 0) \
    X(f1, 0, 1) \
    X(f2, 0, 2) \
    X(f3, 0, 3) \
    X(f4, 0, 4) \
    X(f5, 0, 5) \
    X(f6, 0, 6) \
    X(f7, 0, 7) \
    X(f8, 0, 8) \
    X(f9, 0, 9) \
    X(f10, 0, 10) \
    X(f11, 0, 11) \
    X(f12, 0, 12) \
    \
    X(grave, 1, 0) \
    X(1, 1, 1) \
    X(2, 1, 2) \
    X(3, 1, 3) \
    X(4, 1, 4) \
    X(5, 1, 5) \
    X(6, 1, 6) \
    X(7, 1, 7) \
    X(8, 1, 8) \
    X(9, 1, 9) \
    X(0, 1, 10) \
    X(minus, 1, 11) \
    X(equal, 1, 12) \
    X(sterling, 1, 13) \
    X(backspace, 1, 14) \
    \
    X(tab, 2, 6) \
    X(q, 2, 7) \
    X(w, 2, 8) \
    X(e, 2, 9) \
    X(r, 2, 10) \
    X(t, 2, 11) \
    X(y, 2, 12) \
    X(u, 2, 13) \
    X(i, 2, 14) \
    X(o, 2, 15) \
    X(p, 3, 0) \
    X(bracket_l, 3, 1) \
    X(bracket_r, 3, 2) \
    X(backslash, 3, 3) \
    \
    X(control_l, 3, 11) \
    X(a, 3, 12) \
    X(s, 3, 13) \
    X(d, 3, 14) \
    X(f, 3, 15) \
    X(g, 4, 0) \
    X(h, 4, 1) \
    X(j, 4, 2) \
    X(k, 4, 3) \
    X(l, 4, 4) \
    X(semicolon, 4, 5) \
    X(apostrophe, 4, 6) \
    X(return, 4, 7) \
    \
    X(shift_l, 4, 12) \
    X(z, 4, 14) \
    X(x, 4, 15) \
    X(c, 5, 0) \
    X(v, 5, 1) \
    X(b, 5, 2) \
    X(n, 5, 3) \
    X(m, 5, 4) \
    X(comma, 5, 5) \
    X(period, 5, 6) \
    X(slash, 5, 7) \
    X(shift_r, 5, 8) \
    \
    X(caps_lock, 5, 13) \
    X(alt_l, 5, 14) \
    X(space, 5, 15) \
    X(alt_r, 6, 0) \
    X(control_r, 6, 1) \
    \
    X(print, 0, 13) \
    X(scroll_lock, 0, 14) \
    X(break, 0, 15) \
    X(insert, 1, 15) \
    X(home, 2, 0) \
    X(page_up, 2, 1) \
    X(delete, 3, 4) \
    X(copy, 3, 5) \
    X(page_down, 3, 6) \
    \
    X(up, 5, 9) \
    X(left, 6, 2) \
    X(down, 6, 3) \
    X(right, 6, 4) \
    \
    X(num_lock, 2, 2) \
    X(kp_slash, 2, 3) \
    X(kp_star, 2, 4) \
    X(kp_hash, 2, 5) \
    X(kp_7, 3, 7) \
    X(kp_8, 3, 8) \
    X(kp_9, 3, 9) \
    X(kp_minus, 3, 10) \
    X(kp_4, 4, 8) \
    X(kp_5, 4, 9) \
    X(kp_6, 4, 10) \
    X(kp_plus, 4, 11) \
    X(kp_1, 5, 10) \
    X(kp_2, 5, 11) \
    X(kp_3, 5, 12) \
    X(kp_0, 6, 5) \
    X(kp_decimal, 6, 6) \
    X(kp_enter, 6, 7) \
    \
    X(button_1, 7, 0) \
    X(button_2, 7, 1) \
    X(button_3, 7, 2) \
    X(button_4, 7, 3) \
    X(button_5, 7, 4) \

enum {
#define X(key, row, col) ARCH_KEY_ ## key,
    ARCH_KEYBOARD_DEFINITION
#undef X
};
typedef uint8_t arch_key_id;

typedef struct {
  int8_t KeyColToSend, KeyRowToSend;
  bool KeyUpNDown;
} KbdEntry;

typedef enum {
  KbdState_JustStarted,
  KbdState_SentHardReset,
  KbdState_SentAck1,
  KbdState_SentAck2,
  KbdState_SentKeyByte1, /* and waiting for ack */
  KbdState_SentKeyByte2, /* and waiting for ack */
  KbdState_SentMouseByte1,
  KbdState_SentMouseByte2,
  KbdState_Idle
} KbdStates;

#define KBDBUFFLEN 128

#define KBD_LED_CAPSLOCK 1
#define KBD_LED_NUMLOCK 2
#define KBD_LED_SCROLLLOCK 4

struct arch_keyboard {
  KbdStates KbdState;
  /* A signed, 7-bit value stored in a uint8_t as it gets
   * passed to keyboard transmission functions expecting an
   * uint8_t. */
  uint8_t MouseXCount;
  uint8_t MouseYCount;
  int8_t KeyColToSend,KeyRowToSend;
  bool KeyUpNDown;
  uint8_t Leds;
  /* The bottom three bits of leds holds their current state.  If
   * the bit is set the LED should be emitting. */
  void (*leds_changed)(uint8_t leds);

  /* Double buffering - update the others while sending this */
  uint8_t MouseXToSend;
  uint8_t MouseYToSend;
  bool MouseTransEnable,KeyScanEnable; /* When 1 allowed to transmit */
  uint8_t HostCommand;            /* Normally 0 else the command code */
  KbdEntry Buffer[KBDBUFFLEN];
  uint8_t BuffOcc;
  uint8_t TimerIntHasHappened;
};

#define KBD (*(state->Kbd))

/* ------------------------------------------------------------------ */

/* Tell the Archimedes that key `kid' has changed state, this includes
 * mouse buttons. */
void keyboard_key_changed(struct arch_keyboard *kb, arch_key_id kid,
    bool up);

void Kbd_Init(ARMul_State *state);
void Kbd_StartToHost(ARMul_State *state);
void Kbd_CodeFromHost(ARMul_State *state, uint8_t FromHost);

/* Internal function; just exposed so the profiling code can mess with it */
void Keyboard_Poll(ARMul_State *state,CycleCount nowtime);

/* Frontend must implement this */
int Kbd_PollHostKbd(ARMul_State *state);

#endif

