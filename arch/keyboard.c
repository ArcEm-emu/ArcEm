/* arch/keyboard.c -- a model of the Archimedes keyboard. */

#include "armarc.h"
#include "DispKbd.h"
#include "arch/keyboard.h"

#define DEBUG_KEYBOARD 0

/* ------------------------------------------------------------------ */

/* Information on an arch_key_id. */
typedef struct {
    char *name;
    unsigned char row;
    unsigned char col;
} key_info;

static key_info keys[] = {
#define X(key, row, col) { #key, row, col },
    ARCH_KEYBOARD_DEFINITION
#undef X
};

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
    int up)
{
    key_info *ki;
    KbdEntry *e;

    ki = keys + kid;
    
#if DEBUG_KEYBOARD
    fprintf(stderr, "keyboard_key_changed(kb, \"%s\", %d)\n", ki->name,
        up);
#endif

    if (kb->BuffOcc >= KBDBUFFLEN) {
        fprintf(stderr, "keyboard_key_changed: key \"%s\" discarded, "
            "buffer full\n", ki->name);
        return;
    }

    e = kb->Buffer + kb->BuffOcc++;
    e->KeyColToSend = ki->col;
    e->KeyRowToSend = ki->row;
    e->KeyUpNDown = up;

    return;
}

/*----------------------------------------------------------------------------*/
/* Send the first byte of a data transaction with the host                     */
void Kbd_StartToHost(ARMul_State *state)
{
  /* Start a particular transmission - base don the HostCommand vlaue */
  /* This is either a request for keyboard id, request for mouse, or 0
     if its 0 we should check to see if a key needs sending or mouse data needs
     sending. */
  if (KBD.KbdState!=KbdState_Idle) return;

#ifdef DEBUG_KBD
  if (KBD.HostCommand) fprintf(stderr,"Kbd_StartToHost: HostCommand=%d\n",KBD.HostCommand);
#endif

  switch (KBD.HostCommand) {
    case 0x20: /* Request keyboard id */
        if (IOC_WriteKbdRx(state, PROTO_KBID_UK) != -1) {
        KBD.KbdState=KbdState_Idle;
        KBD.HostCommand=0;
      };
      return;

    case 0x22: /* Request mouse position */
      KBD.MouseXToSend=KBD.MouseXCount;
      KBD.MouseYToSend=KBD.MouseYCount;
      if (IOC_WriteKbdRx(state,KBD.MouseXToSend)!=-1) {
        KBD.KbdState=KbdState_SentMouseByte1;
        KBD.HostCommand=0;
      };
      return;
  };

  /* OK - we'll have a look to see if we should be doing something else */
  if (KBD.KbdState!=KbdState_Idle) return;

  /* Perhaps some keyboard data */
  if (KBD.KeyScanEnable && (KBD.BuffOcc>0)) {
    int loop;
#ifdef DEBUG_KBD
    fprintf(stderr,"KBD_StartToHost - sending key -  BuffOcc=%d (%d,%d,%d)\n",KBD.BuffOcc,
       KBD.Buffer[0].KeyUpNDown,KBD.Buffer[0].KeyRowToSend,KBD.Buffer[0].KeyColToSend);
#endif
    KBD.KeyUpNDown=KBD.Buffer[0].KeyUpNDown;
    KBD.KeyRowToSend=KBD.Buffer[0].KeyRowToSend;
    KBD.KeyColToSend=KBD.Buffer[0].KeyColToSend;
    /* I should implement a circular buffer - but can't be bothered yet */
    for(loop=1;loop<KBD.BuffOcc;loop++)
      KBD.Buffer[loop-1]=KBD.Buffer[loop];

    if (IOC_WriteKbdRx(state,(KBD.KeyUpNDown?0xd0:0xc0) | KBD.KeyRowToSend)!=-1) {
      KBD.KbdState=KbdState_SentKeyByte1;
    };
    KBD.BuffOcc--;
    return;
  };

  /* NOTE: Mouse movement gets lower priority than key input */
  if ((KBD.MouseTransEnable) && (KBD.MouseXCount | KBD.MouseYCount)) {
    /* Send some mouse data why not! */
    KBD.MouseXToSend=KBD.MouseXCount;
    KBD.MouseYToSend=KBD.MouseYCount;
    KBD.MouseXCount=0;
    KBD.MouseYCount=0;
    if (IOC_WriteKbdRx(state,KBD.MouseXToSend)!=-1) {
      KBD.KbdState=KbdState_SentMouseByte1;
    };
    return;
  };

}

/*----------------------------------------------------------------------------*/
/* Called when their is some data in the serial tx register on the IOC         */
void Kbd_CodeFromHost(ARMul_State *state, unsigned char FromHost)
{
#ifdef DEBUG_KBD
  fprintf(stderr,"Kbd_CodeFromHost: FromHost=0x%x State=%d\n",FromHost,KBD.KbdState);
#endif

  switch (KBD.KbdState) {
    case KbdState_JustStarted:
      /* Only valid code is Reset */
        if (FromHost == PROTO_HRST) {
            if (IOC_WriteKbdRx(state, PROTO_HRST) != -1) {
          KBD.KbdState=KbdState_SentHardReset;
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received Reset and sent Reset\n");
#endif
        } else {
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Couldn't respond to Reset - Kart full\n");
#endif
        };
      } else {
#ifdef DEBUG_KBD
        fprintf(stderr,"KBD: JustStarted; Got bad code 0x%x\n",FromHost);
#endif
      };
      break;

    case KbdState_SentHardReset:
      /* Only valid code is ack1 */
        if (FromHost == PROTO_RAK1) {
            if (IOC_WriteKbdRx(state, PROTO_RAK1) != -1) {
          KBD.KbdState=KbdState_SentAck1;
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received Ack1 and sent Ack1\n");
#endif
        } else {
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Couldn't respond to Ack1 - Kart full\n");
#endif
        };
      } else {
#ifdef DEBUG_KBD
        fprintf(stderr,"KBD: SentAck1; Got bad code 0x%x - sending reset\n",FromHost);
#endif
            IOC_WriteKbdRx(state, PROTO_HRST);
        KBD.KbdState=KbdState_SentHardReset; /* Or should that be just started? */
      };
      break;

    case KbdState_SentAck1:
      /* Only valid code is Reset Ak 2 */
        if (FromHost == PROTO_RAK2) {
            if (IOC_WriteKbdRx(state, PROTO_RAK2) != -1) {
          KBD.KbdState=KbdState_SentAck2;
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received and ack'd to Ack2\n");
#endif
        } else {
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Couldn't respond to Ack2 - Kart full\n");
#endif
        };
      } else {
#ifdef DEBUG_KBD
        fprintf(stderr,"KBD: SentAck2; Got bad code 0x%x\n",FromHost);
#endif
      };
      break;

    default:
        if (FromHost == PROTO_HRST) {
            if (IOC_WriteKbdRx(state, PROTO_HRST) != -1) {
          KBD.KbdState=KbdState_SentHardReset;
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received and ack'd to hardware reset\n");
#endif
        } else {
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Couldn't respond to hardware reset - Kart full\n");
#endif
        };
        return;
      };

      switch (FromHost & 0xf0) {
        case 0: /* May be LED switch */
          if ((FromHost & 0x08)==0x08) {
#ifdef DEBUG_KBD
            fprintf(stderr,"KBD: Received bad code: 0x%x\n",FromHost);
#endif
            return;
          }
#ifdef DEBUG_KBD
          /*printf("KBD: LED state now: 0x%x\n",FromHost & 0x7); */
#endif
          if (KBD.Leds!=(FromHost & 0x7)) {
            KBD.Leds=FromHost & 0x7;
                if (KBD.leds_changed) {
                    (*KBD.leds_changed)(KBD.Leds);
                }
          };
#ifdef LEDENABLE
          /* I think we have to acknowledge - but I don't know with what */
          if (IOC_WriteKbdRx(state,0xa0 | (FromHost & 0x7))) {
#ifdef DEBUG_KBD
            fprintf(stderr,"KBD: acked led's\n");
#endif
          } else {
#ifdef DEBUG_KBD
            fprintf(stderr,"KBD: Couldn't respond to LED - Kart full\n");
#endif
          };
#endif
          break;

        case 0x20: /* Host requests keyboard id - or mouse position */
          KBD.HostCommand=FromHost;
          Kbd_StartToHost(state);
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Host requested keyboard id\n");
#endif
          return;

        case 0x30: /* Its probably an ack of some type */
          switch (FromHost & 0xf) {
            case 0: /* Disables everything */
            case 1: /* Disables mouse */
            case 2: /* Disables keys */
            case 3: /* all enabled */
              if ((KBD.KbdState!=KbdState_SentKeyByte1) &&
                  (KBD.KbdState!=KbdState_SentMouseByte1)) {
                KBD.KbdState=KbdState_Idle;
                KBD.MouseTransEnable=(FromHost & 2)>>1;
                KBD.KeyScanEnable=FromHost & 1;
              } else {
                /* Hmm - we just sent a first byte - we shouldn't get one of these! */
#ifdef DEBUG_KBD
                fprintf(stderr,"KBD: Got last byte ack after first byte!\n");
#endif
              };
              break;

            case 0xf: /* First byte ack */
              if ((KBD.KbdState!=KbdState_SentKeyByte1) &&
                  (KBD.KbdState!=KbdState_SentMouseByte1)) {
#ifdef DEBUG_KBD
                fprintf(stderr,"KBD: Got 1st byte ack when we haven't sent one!\n");
#endif
              } else {
                if (KBD.KbdState==KbdState_SentMouseByte1) {
                  if (IOC_WriteKbdRx(state,KBD.MouseYToSend)==-1) {
#ifdef DEBUG_KBD
                    fprintf(stderr,"KBD: Couldn't send 2nd byte of mouse value - Kart full\n");
#endif
                  };
                  KBD.KbdState=KbdState_SentMouseByte2;
                } else {
                  if (IOC_WriteKbdRx(state,(KBD.KeyUpNDown?0xd0:0xc0) | KBD.KeyColToSend)==-1) {
#ifdef DEBUG_KBD
                    fprintf(stderr,"KBD: Couldn't send 2nd byte of key value - Kart full\n");
#endif
                  };
                  KBD.KbdState=KbdState_SentKeyByte2;
                  /* Indicate that the key has been sent */
                  KBD.KeyRowToSend=-1;
                  KBD.KeyColToSend=-1;
                };
              }; /* Have sent 1st byte test */
              break;

            default:
#ifdef DEBUG_KBD
              fprintf(stderr,"KBD: Bad ack type received 0x%x\n",FromHost);
#endif
              break;
          }; /* bottom nybble of ack switch */
          return;

        case 0x40: /* Host just sends us some data....*/
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Host sent us some data: 0x%x\n",FromHost);
#endif
          return;

        default:
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Unknown code received from host 0x%x\n",FromHost);
#endif
          return;
      }; /* FromHost top nybble switch */
  }; /* current state switch */
}
