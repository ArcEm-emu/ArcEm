/* arch/keyboard.c -- a model of the Archimedes keyboard. */

#include "armarc.h"
#include "dbugsys.h"
#include "keyboard.h"

/* ------------------------------------------------------------------ */

#if defined(DEBUG_KEYBOARD) || defined(WARN_KEYBOARD)
#define STORE_KEY_NAME 1
#else
#define STORE_KEY_NAME 0
#endif

/* Information on an arch_key_id. */
static const KbdEntry keys[] = {
#define X(key, row, col) { row, col, 0 },
  ARCH_KEYBOARD_DEFINITION
#undef X
};

#if STORE_KEY_NAME
static const char *const key_names[] = {
#define X(key, row, col) #key,
  ARCH_KEYBOARD_DEFINITION
#undef X
};
#endif

/* The protocol between the keyboard and Archimedes. */

/* Keyboard reset. */
#define PROTO_HRST 0xff
/* Reset acknowledge 1. */
#define PROTO_RAK1 0xfe
/* Reset acknowledge 2. */
#define PROTO_RAK2 0xfd
/* Archimedes UK keyboard id. */
#define PROTO_KBID_UK 0x81

/* ------------------------------------------------------------------ */

void keyboard_key_changed(struct arch_keyboard *kb, arch_key_id kid,
                          bool up)
{
  const KbdEntry *ki;
  KbdEntry e;

  ki = keys + kid;
  e.KeyColToSend = ki->KeyColToSend;
  e.KeyRowToSend = ki->KeyRowToSend;
  e.KeyUpNDown = up;

#if STORE_KEY_NAME
  dbug_kbd("keyboard_key_changed(kb, \"%s\", %d)\n", key_names[kid], up);
#endif

  if ((kb->BuffWritePos + 1) % KBDBUFFLEN == kb->BuffReadPos) {
#if STORE_KEY_NAME
    warn_kbd("keyboard_key_changed: key \"%s\" discarded, "
             "buffer full\n", key_names[kid]);
#endif
  } else {
    kb->Buffer[kb->BuffWritePos] = e;
    kb->BuffWritePos = (kb->BuffWritePos + 1) % KBDBUFFLEN;
  }

  return;
}

void keyboard_key_changed_ex(struct arch_keyboard *kb, uint8_t row,
                             uint8_t col, bool up)
{
  KbdEntry e;

  e.KeyColToSend = col;
  e.KeyRowToSend = row;
  e.KeyUpNDown = up;

#if STORE_KEY_NAME
  dbug_kbd("keyboard_key_changed_ex(kb, %d, %d, %d)\n", row, col, up);
#endif

  if ((kb->BuffWritePos + 1) % KBDBUFFLEN == kb->BuffReadPos) {
#if STORE_KEY_NAME
    warn_kbd("keyboard_key_changed: key (%d, %d) discarded, "
             "buffer full\n", row, col);
#endif
  } else {
    kb->Buffer[kb->BuffWritePos] = e;
    kb->BuffWritePos = (kb->BuffWritePos + 1) % KBDBUFFLEN;
  }

  return;
}

/* ------------------------------------------------------------------ */

/* Send the first byte of a data transaction with the host. */

void Kbd_StartToHost(ARMul_State *state)
{
  /* Start a particular transmission - based on the HostCommand
   * value.  This is either a request for keyboard id, request for
   * mouse, or 0.  If it's 0 we should check to see if a key needs
   * sending or mouse data needs sending. */

  if (KBD.KbdState != KbdState_Idle) {
    return;
  }

  if (KBD.HostCommand) {
    dbug_kbd("Kbd_StartToHost: HostCommand=%d\n", KBD.HostCommand);
  }

  switch (KBD.HostCommand) {
    case 0x20: /* Request keyboard id */
      if (IOC_WriteKbdRx(state, PROTO_KBID_UK) != -1) {
        KBD.KbdState = KbdState_Idle;
        KBD.HostCommand = 0;
      }
      return;
    case 0x22: /* Request mouse position */
      KBD.MouseXToSend = KBD.MouseXCount;
      KBD.MouseYToSend = KBD.MouseYCount;
      if (IOC_WriteKbdRx(state, KBD.MouseXToSend) != -1) {
        KBD.KbdState = KbdState_SentMouseByte1;
        KBD.HostCommand = 0;
      }
      return;
  }

  /* OK - we'll have a look to see if we should be doing something
   * else */
  if (KBD.KbdState != KbdState_Idle) {
    return;
  }

  /* Perhaps some keyboard data */
  if (KBD.KeyScanEnable) {
    uint8_t BuffReadPos, BuffWritePos;
    KbdEntry e;

    BuffReadPos = KBD.BuffReadPos;
    BuffWritePos = KBD.BuffWritePos;
    if (BuffReadPos != BuffWritePos) {
      e = KBD.Buffer[BuffReadPos];
      KBD.BuffReadPos = (BuffReadPos + 1) % KBDBUFFLEN;

      dbug_kbd("KBD_StartToHost - sending key -  BuffReadPos=%d "
          "BuffWritePos=%d (%d,%d,%d)\n", BuffReadPos, BuffWritePos,
          e.KeyUpNDown, e.KeyRowToSend, e.KeyColToSend);

      KBD.KeyUpNDown = e.KeyUpNDown;
      KBD.KeyRowToSend = e.KeyRowToSend;
      KBD.KeyColToSend = e.KeyColToSend;

      if (IOC_WriteKbdRx(state, (uint8_t) ((KBD.KeyUpNDown ? 0xd0 : 0xc0) | KBD.KeyRowToSend)) != -1) {
        KBD.KbdState = KbdState_SentKeyByte1;
      }
      return;
    }
  }

  /* NOTE:  Mouse movement gets lower priority than key input. */
  if (KBD.MouseTransEnable && KBD.MouseXCount | KBD.MouseYCount) {
    /* Send some mouse data why not! */
    KBD.MouseXToSend = KBD.MouseXCount;
    KBD.MouseYToSend = KBD.MouseYCount;
    KBD.MouseXCount = 0;
    KBD.MouseYCount = 0;
    if (IOC_WriteKbdRx(state, KBD.MouseXToSend) != -1) {
      KBD.KbdState = KbdState_SentMouseByte1;
    }
    return;
  }

  return;
}

/* ------------------------------------------------------------------ */

/* Called when there is some data in the serial tx register on the
 * IOC. */

void Kbd_CodeFromHost(ARMul_State *state, uint8_t FromHost)
{
  dbug_kbd("Kbd_CodeFromHost: FromHost=0x%x State=%d\n",
      FromHost, KBD.KbdState);

  switch (KBD.KbdState) {
    case KbdState_JustStarted:
      /* Only valid code is Reset. */
      if (FromHost == PROTO_HRST) {
        if (IOC_WriteKbdRx(state, PROTO_HRST) != -1) {
          KBD.KbdState = KbdState_SentHardReset;
          dbug_kbd("KBD: Received Reset and sent Reset\n");
        } else {
          dbug_kbd("KBD: Couldn't respond to Reset - Kart "
              "full\n");
        }
      } else {
        dbug_kbd("KBD: JustStarted; Got bad code 0x%x\n",
            FromHost);
      }
      break;

    case KbdState_SentHardReset:
      /* Only valid code is ack1 */
      if (FromHost == PROTO_RAK1) {
        if (IOC_WriteKbdRx(state, PROTO_RAK1) != -1) {
          KBD.KbdState = KbdState_SentAck1;
          dbug_kbd("KBD: Received Ack1 and sent Ack1\n");
        } else {
          dbug_kbd("KBD: Couldn't respond to Ack1 - Kart "
              "full\n");
        }
      } else {
        dbug_kbd("KBD: SentAck1; Got bad code 0x%x - "
            "sending reset\n", FromHost);
        IOC_WriteKbdRx(state, PROTO_HRST);
        KBD.KbdState = KbdState_SentHardReset;
        /* Or should that be just started? */
      }
      break;

    case KbdState_SentAck1:
      /* Only valid code is Reset Ak 2 */
      if (FromHost == PROTO_RAK2) {
        if (IOC_WriteKbdRx(state, PROTO_RAK2) != -1) {
          KBD.KbdState = KbdState_SentAck2;
          dbug_kbd("KBD: Received and ack'd to Ack2\n");
        } else {
          dbug_kbd("KBD: Couldn't respond to Ack2 - Kart "
              "full\n");
        }
      } else {
        dbug_kbd("KBD: SentAck2; Got bad code 0x%x\n",
            FromHost);
      }
      break;

    default:
      if (FromHost == PROTO_HRST) {
        if (IOC_WriteKbdRx(state, PROTO_HRST) != -1) {
          KBD.KbdState = KbdState_SentHardReset;
          dbug_kbd("KBD: Received and ack'd to hardware "
              "reset\n");
        } else {
          dbug_kbd("KBD: Couldn't respond to hardware reset - "
              "Kart full\n");
        }
        return;
      }

      switch (FromHost & 0xf0) {
        case 0: /* May be LED switch */
          if ((FromHost & 0x08) == 0x08) {
            dbug_kbd("KBD: Received bad code: 0x%x\n",
                FromHost);
             return;
          }
          dbug_kbd("KBD: LED state now: 0x%x\n", FromHost & 0x7);
          if (KBD.Leds != (FromHost & 0x7)) {
            KBD.Leds = FromHost & 0x7;
            if (KBD.leds_changed) {
              (*KBD.leds_changed)(KBD.Leds);
            }
          }
#ifdef LEDENABLE
          /* I think we have to acknowledge - but I don't know with
           * what. */
          if (IOC_WriteKbdRx(state, 0xa0 | FromHost & 0x7)) {
            dbug_kbd("KBD: acked led's\n");
          } else {
            dbug_kbd("KBD: Couldn't respond to LED - Kart "
                 "full\n");
          }
#endif
          break;

        case 0x20: /* Host requests keyboard id - or mouse position. */
          KBD.HostCommand = FromHost;
          Kbd_StartToHost(state);
          dbug_kbd("KBD: Host requested keyboard id\n");
          return;

        case 0x30: /* Its probably an ack of some type. */
          switch (FromHost & 0xf) {
          case 0: /* Disables everything. */
          case 1: /* Disables mouse. */
          case 2: /* Disables keys. */
          case 3: /* all enabled. */
            if (KBD.KbdState != KbdState_SentKeyByte1 &&
                KBD.KbdState != KbdState_SentMouseByte1) {
                KBD.KbdState = KbdState_Idle;
                KBD.MouseTransEnable = (FromHost & 2) >> 1;
                KBD.KeyScanEnable = FromHost & 1;
            } else {
                /* Hmm - we just sent a first byte - we shouldn't get
                 * one of * these! */
                dbug_kbd("KBD: Got last byte ack after first "
                    "byte!\n");
            }
            break;

        case 0xf: /* First byte ack. */
          if (KBD.KbdState != KbdState_SentKeyByte1 &&
            KBD.KbdState != KbdState_SentMouseByte1)
          {
            dbug_kbd("KBD: Got 1st byte ack when we "
                 "haven't sent one!\n");
          } else {
            if (KBD.KbdState == KbdState_SentMouseByte1) {
              if (IOC_WriteKbdRx(state, KBD.MouseYToSend) == -1) {
                  dbug_kbd("KBD: Couldn't send 2nd byte "
                      "of mouse value - Kart full\n");
              }
              KBD.KbdState = KbdState_SentMouseByte2;
            } else {
              if (IOC_WriteKbdRx(state,
                  (uint8_t) ((KBD.KeyUpNDown ? 0xd0 : 0xc0) |
                  KBD.KeyColToSend)) == -1)
              {
                  dbug_kbd("KBD: Couldn't send 2nd byte "
                      "of key value - Kart full\n");
              }
              KBD.KbdState = KbdState_SentKeyByte2;
              /* Indicate that the key has been sent. */
              KBD.KeyRowToSend = -1;
              KBD.KeyColToSend = -1;
            }
          } /* Have sent 1st byte test. */
          break;

        default:
          dbug_kbd("KBD: Bad ack type received 0x%x\n",
              FromHost);
          break;
      }
      return;

      case 0x40: /* Host just sends us some data. */
        dbug_kbd("KBD: Host sent us some data: 0x%x\n",
            FromHost);
        return;

      default:
        dbug_kbd("KBD: Unknown code received from host "
            "0x%x\n", FromHost);
        return;
    }
  }

  return;
}

void Keyboard_Poll(ARMul_State *state,CycleCount nowtime)
{
  int KbdSerialVal;
  EventQ_RescheduleHead(state,nowtime+12500,Keyboard_Poll); /* TODO - Should probably be realtime */
  /* Call host-specific routine */
  Kbd_PollHostKbd(state);
  /* Keyboard check */
  KbdSerialVal = IOC_ReadKbdTx(state);
  if (KbdSerialVal != -1) {
    Kbd_CodeFromHost(state, (uint8_t) KbdSerialVal);
  } else {
    if (KBD.TimerIntHasHappened > 2) {
      KBD.TimerIntHasHappened = 0;
      if (KBD.KbdState == KbdState_Idle) {
        Kbd_StartToHost(state);
      }
    }
  }
}

void Kbd_Init(ARMul_State *state)
{
  static arch_keyboard kbd;
  state->Kbd = &kbd;

  KBD.KbdState            = KbdState_JustStarted;
  KBD.MouseTransEnable    = false;
  KBD.KeyScanEnable       = false;
  KBD.KeyColToSend        = -1;
  KBD.KeyRowToSend        = -1;
  KBD.MouseXCount         = 0;
  KBD.MouseYCount         = 0;
  KBD.KeyUpNDown          = false; /* When false it means the key to be sent is a key down event, true is up */
  KBD.HostCommand         = 0;
  KBD.BuffReadPos         = 0;
  KBD.BuffWritePos        = 0;
  KBD.TimerIntHasHappened = 0; /* If using AutoKey should be 2 Otherwise it never reinitialises the event routines */
  KBD.Leds                = 0;
  KBD.leds_changed        = NULL;

  EventQ_Insert(state,ARMul_Time+12500,Keyboard_Poll);
}

