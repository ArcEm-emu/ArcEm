/*
  DispKbdShared.c

  Shared between all platform ports. Shared Parts of Display and Input
  layer 

  (c) 1996-2005 D Gilbert, P Howkins, et al

  Part of Arcem, covered under the GNU GPL, see file COPYING for details

*/

#include <string.h>

#include "armarc.h"


/* Shared between X/Mac OS X/Windows */

/**
 * get_pixelval
 *
 *
 *
 * @param red
 * @param green
 * @param blue
 * @returns
 */
unsigned long get_pixelval(unsigned int red, unsigned int green, unsigned int blue)
{
  return (((red  >> (16 - HOSTDISPLAY.red_prec))   << HOSTDISPLAY.red_shift) +
         ((green >> (16 - HOSTDISPLAY.green_prec)) << HOSTDISPLAY.green_shift) +
         ((blue  >> (16 - HOSTDISPLAY.blue_prec))  << HOSTDISPLAY.blue_shift));
}



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
