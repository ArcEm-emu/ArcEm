/*
  DispKbdShared.c

  Shared between all platform ports. Shared Parts of Display and Input
  layer

  (c) 1996-2005 D Gilbert, P Howkins, et al

  Part of Arcem, covered under the GNU GPL, see file COPYING for details

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch/armarc.h"
#include "arch/hdc63463.h"
#include "arch/keyboard.h"
#ifdef SOUND_SUPPORT
#include "arch/sound.h"
#endif

#define DC DISPLAYCONTROL

static struct EventNode enodes[4];
static int xpollenode = 2; /* Flips between 2 and 3 */

/**
 * MarkAsUpdated
 *
 *
 *
 * I'm not confident that this is completely correct - if it's wrong all hell
 * is bound to break loose! If it works however it should speed things up nicely!
 *
 * @param state
 * @param end
 */
void MarkAsUpdated(ARMul_State *state, int end)
{
  unsigned int Addr = MEMC.Vinit * 16;
  unsigned int Vend = MEMC.Vend  * 16;

  /* Loop from Vinit until we have done the whole image */

  for( ; end > 0; Addr += UPDATEBLOCKSIZE, end -= UPDATEBLOCKSIZE) {
    if (Addr > Vend) {
      Addr = Addr - Vend + MEMC.Vstart * 16;
    }
    DISPLAYCONTROL.UpdateFlags[Addr / UPDATEBLOCKSIZE] = MEMC.UpdateFlags[Addr / UPDATEBLOCKSIZE];
  }

}

/**
 * QueryRamChange
 *
 * Check to see if the area of memory has changed.
 * Returns true if there is any chance that the given area has changed
 *
 * @param state  IMPROVE Unused
 * @param offset Pointer to section of RAM to check
 * @param len    Amount of RAM to check (in bytes)
 * @returns      bool of whether the ram
 */
int QueryRamChange(ARMul_State *state, unsigned int offset, int len)
{
  unsigned int Vinit  = MEMC.Vinit;
  unsigned int Vstart = MEMC.Vstart;
  unsigned int Vend   = MEMC.Vend;
  unsigned int startblock, endblock, currentblock;

  /* Figure out if 'offset' starts between Vinit-Vend or Vstart-Vinit */

  if (offset < (((Vend - Vinit) + 1) * 16)) {
    /* Vinit-Vend */
    /* Now check to see if the whole buffer is in that area */
    if ((offset + len) >= (((Vend - Vinit) + 1) * 16)) {
      /* Its split - so copy the bit upto Vend and then the rest */

      /* Don't bother - this isn't going to happen much - lets say it changed */
      return(1);
    } else {
      offset += Vinit * 16;
    }
  } else {
    /* Vstart-Vinit */
    /* its all in one place */
    offset -= ((Vend - Vinit) + 1) * 16; /* Thats the bit after Vinit */
    offset += Vstart * 16; /* so the bit we copy is somewhere after Vstart */
  }

  /* So now we have an area running from 'offset' to 'offset+len' */
  startblock = offset / UPDATEBLOCKSIZE;
  endblock   = (offset + len) / UPDATEBLOCKSIZE;

  /* Now just loop through from startblock to endblock */
  for(currentblock = startblock; currentblock <= endblock; currentblock++) {
    if (MEMC.UpdateFlags[currentblock] != DISPLAYCONTROL.UpdateFlags[currentblock]) {
      return(1);
    }
  }

  /* We've checked them all and their are no changes */
  return(0);
}

/**
 * CopyScreenRAM
 *
 * Copy a lump of screen RAM into the buffer.  The section of screen ram is
 * len bytes from the top left of the screen.  The routine takes into account
 * all scrolling etc.
 * This routine is burdened with undoing endianness
 *
 * @param state   IMPROVE Unused
 * @param offset
 * @param len
 * @param Buffer
 */
void CopyScreenRAM(ARMul_State *state, unsigned int offset, int len, char *Buffer)
{
  unsigned int Vinit  = MEMC.Vinit;
  unsigned int Vstart = MEMC.Vstart;
  unsigned int Vend   = MEMC.Vend;

  /*fprintf(stderr,"CopyScreenRAM: offset=%d len=%d Vinit=0x%x VStart=0x%x Vend=0x%x\n",
         offset,len,Vinit,Vstart,Vend); */

  /* Figure out if 'offset' starts between Vinit-Vend or Vstart-Vinit */
  if ((offset) < (((Vend - Vinit) + 1) * 16)) {
    /* Vinit-Vend */
    /* Now check to see if the whole buffer is in that area */
    if ((offset + len) >= (((Vend - Vinit) + 1) * 16)) {
      /* Its split - so copy the bit upto Vend and then the rest */
      int tmplen;

      offset += Vinit * 16;
      tmplen  = (Vend + 1) * 16 - offset;
      /*fprintf(stderr,"CopyScreenRAM: Split over Vend offset now=0x%x tmplen=%d\n",offset,tmplen); */
      memcpy(Buffer, MEMC.PhysRam + (offset / sizeof(ARMword)), tmplen);
      memcpy(Buffer + tmplen, MEMC.PhysRam + ((Vstart * 16) / sizeof(ARMword)), len-tmplen);
    } else {
      /* Its all their */
      /*fprintf(stderr,"CopyScreenRAM: All in one piece between Vinit..Vend offset=0x%x\n",offset); */
      offset += Vinit * 16;
      memcpy(Buffer, MEMC.PhysRam + (offset / sizeof(ARMword)), len);
    }
  } else {
    /* Vstart-Vinit */
    /* its all in one place */
    offset -= ((Vend - Vinit) + 1) * 16; /* Thats the bit after Vinit */
    offset += Vstart * 16; /* so the bit we copy is somewhere after Vstart */
    /*fprintf(stderr,"CopyScreenRAM: All in one piece between Vstart..Vinit offset=0x%x\n",offset); */
    memcpy(Buffer, MEMC.PhysRam + (offset / sizeof(ARMword)), len);
  }

#ifdef HOST_BIGENDIAN
/* Hacking of the buffer now - OK - I know that I should do this neater */
  for(offset = 0; offset < len; offset += 4) {
    unsigned char tmp;

    tmp = Buffer[offset];
    Buffer[offset] = Buffer[offset + 3];
    Buffer[offset + 3] = tmp;
    tmp = Buffer[offset + 2];
    Buffer[offset + 2] = Buffer[offset + 1];
    Buffer[offset + 1] = tmp;
  }
#endif
}

void
DisplayKbd_Init(ARMul_State *state)
{
  /* Call Host-specific routine */
  DisplayKbd_InitHost(state);

  DC.PollCount            = 0; /* Seems to never be used */
  KBD.KbdState            = KbdState_JustStarted;
  KBD.MouseTransEnable    = 0;
  KBD.KeyScanEnable       = 0;
  KBD.KeyColToSend        = -1;
  KBD.KeyRowToSend        = -1;
  KBD.MouseXCount         = 0;
  KBD.MouseYCount         = 0;
  KBD.KeyUpNDown          = 0; /* When 0 it means the key to be sent is a key down event, 1 is up */
  KBD.HostCommand         = 0;
  KBD.BuffOcc             = 0;
  KBD.TimerIntHasHappened = 0; /* If using AutoKey should be 2 Otherwise it never reinitialises the event routines */
  KBD.Leds                = 0;
  KBD.leds_changed        = NULL;
  DC.DoingMouseFollow     = 0;

  /* Note the memory model sets its to 1 - note the ordering */
  /* i.e. it must be updated */
  memset(DC.UpdateFlags, 0,
         ((512 * 1024) / UPDATEBLOCKSIZE) * sizeof(unsigned));

#ifdef SOUND_SUPPORT
  /* Initialise the Sound output */
  if (sound_init()) {
    /* There was an error of some sort - it will already have been reported */
    fprintf(stderr, "Could not initialise sound output - exiting\n");
    exit(EXIT_FAILURE);
  }
#endif

  ARMul_ScheduleEvent(&enodes[xpollenode], POLLGAP, DisplayKbd_Poll);
  xpollenode ^= 1;
} /* DisplayKbd_Init */

/* Called using an ARM_ScheduleEvent - it also sets itself up to be called
   again.                                                                     */

unsigned
DisplayKbd_Poll(void *data)
{
  ARMul_State *state = data;
  int KbdSerialVal;
  static int KbdPollInt = 0;
  static int discconttog = 0;

#ifdef SOUND_SUPPORT
  sound_poll();
#endif

#ifndef SYSTEM_gp2x
  /* Our POLLGAP runs at 125 cycles, HDC (and fdc) want callback at 250 */
  if (discconttog) {
    FDC_Regular(state);
    HDC_Regular(state);
  }
  discconttog ^= 1;
#endif

  if ((KbdPollInt++) > 100) {
    KbdPollInt = 0;
    /* Keyboard check */
    KbdSerialVal = IOC_ReadKbdTx(state);
    if (KbdSerialVal != -1) {
      Kbd_CodeFromHost(state, (unsigned char) KbdSerialVal);
    } else {
      if (KBD.TimerIntHasHappened > 2) {
        KBD.TimerIntHasHappened = 0;
        if (KBD.KbdState == KbdState_Idle) {
          Kbd_StartToHost(state);
        }
      }
    }
  }

  /* Call Host-specific routine */
  if ( DisplayKbd_PollHost(state) )
    KbdPollInt = 1000;

  if (--(DC.AutoRefresh) < 0) {
    RefreshDisplay(state);
  }

  ARMul_ScheduleEvent(&enodes[xpollenode], POLLGAP, DisplayKbd_Poll);
  xpollenode ^= 1;

  return 0;
} /* DisplayKbd_Poll */
