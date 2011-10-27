/*
  arch/displaydev.c

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Part of Arcem released under the GNU GPL, see file COPYING
  for details.

  Utility functions for bridging the gap between the host display code and the
  emulator core
*/
#include "armdefs.h"
#include "displaydev.h"
#include "archio.h"

const DisplayDev *DisplayDev_Current = NULL;

int DisplayDev_Set(ARMul_State *state,const DisplayDev *dev)
{
  struct Vidc_Regs Vidc;
  if(DisplayDev_Current)
  {
    Vidc = VIDC;
    (DisplayDev_Current->Shutdown)(state);
    DisplayDev_Current = NULL;
  }
  else
  {
    memset(&Vidc,0,sizeof(Vidc));
  }
  if(dev)
  {
    int ret = (dev->Init)(state,&Vidc);
    if(ret)
      return ret;
    DisplayDev_Current = dev;
  }
  return 0;
}

void DisplayDev_GetCursorPos(ARMul_State *state,int *x,int *y)
{
  static const signed char offsets[4] = {19-6,11-6,7-6,5-6};
  *x = VIDC.Horiz_CursorStart-(VIDC.Horiz_DisplayStart*2+offsets[(VIDC.ControlReg & 0xc)>>2]);
  *y = VIDC.Vert_CursorStart-VIDC.Vert_DisplayStart;
}

static const unsigned long vidcclocks[4] = {24000000,25175000,36000000,24000000};

unsigned long DisplayDev_GetVIDCClockIn(void)
{
  return vidcclocks[ioc.IOEBControlReg & IOEB_CR_VIDC_MASK];
}

/*

  Routine to copy a bitstream from one bit-aligned location to another

  dest & src pointers must be ARMword aligned
  destalign & srcalign must be 0 <= x < 32
  count is in bits

*/

void BitCopy(ARMword *dest,int destalign,const ARMword *src,int srcalign,int count)
{
  /* Get the destination word aligned */
  int invdestalign = 32-destalign;
  int invsrcalign = 32-srcalign;
  ARMword tempsrc1 = *src++;
  ARMword tempsrc2 = *src++;
  if(destalign)
  {
    ARMword tempdest = *dest;

    ARMword mask = (count<=invdestalign?(1<<count):0)-1;
    mask = mask<<destalign;
    tempdest &= ~mask;
    ARMword tempsrc3 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    tempdest |= (tempsrc1<<destalign) & mask;
    *dest++ = tempdest;

    count -= invdestalign;
    
    if(count <= 0)
    {
      /* Nothing else to copy */
      return;
    }

    srcalign += invdestalign;
    if(srcalign >= 32)
    {
      tempsrc1 = tempsrc2;
      tempsrc2 = *src++;
      srcalign &= 0x1f;
    }
    invsrcalign = 32-srcalign;
  }
  if(!srcalign && (count>128)) /* TODO - Tweak this value */
  {
    /* Matching alignment, memcpy up to the end */
    int temp = count>>5;
    memcpy(dest,src-2,temp<<2);
    dest += temp;
    tempsrc1 = src[temp-2];
    count &= 0x1f;
  }
  else while(count >= 32)
  {
    *dest++ = (tempsrc1>>srcalign) | (tempsrc2<<invsrcalign);
    tempsrc1 = tempsrc2;
    tempsrc2 = *src++;
    count -= 32;
  }
  if(count)
  {
    /* End bits */
    ARMword tempdest = *dest;
    ARMword mask = 0xffffffff<<count;
    tempdest &= mask;
    tempsrc1 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc1 |= (tempsrc2<<invsrcalign);
    tempdest |= tempsrc1 &~ mask;
    *dest = tempdest;
  }
}

/*

  Return size (entry count) for expand table

  srcbpp = src BPP (1,2,4,8)
  factor = log2 of how much bigger each dest pixel is (1 <= factor <= log2(32/srcbpp))

*/

int GetExpandTableSize(unsigned int srcbpp,unsigned int factor)
{
  /* Work out how many source pixels we can lookup before we reach:
     (a) 32 output bits
     or
     (b) 256 table entries
  */
  unsigned int destbpp = srcbpp<<factor;
  unsigned int pixels = 1;
  while((destbpp*pixels < 32) && (srcbpp*pixels < 8))
    pixels <<= 1; /* Should only need to consider powers of two */
  unsigned int srcbits = 1<<(srcbpp*pixels);
  return srcbits;
}

/*

  Routine to generate lookup tables for fast upscaling/expansion

  dest = destination buffer, must be appropriate size
  srcbpp = src BPP (1,2,4,8)
  factor = log2 of how much bigger each dest pixel is (1 <= factor <= log2(32/srcbpp))
  mul = multiplier (0x1 for simple BPP conversion, 0x11 for 4-bit 2X upscale, 0x101 for 8-bit 2X upscale, etc.)

*/

void GenExpandTable(ARMword *dest,unsigned int srcbpp,unsigned int factor,unsigned int mul)
{
  unsigned int destbpp = srcbpp<<factor;
  unsigned int pixels = 1;
  while((destbpp*pixels < 32) && (srcbpp*pixels < 8))
    pixels <<= 1;
  unsigned int srcbits = 1<<(srcbpp*pixels);
  ARMword i,j;
  for(i=0;i<srcbits;i++)
  {
    ARMword out = 0;
    for(j=0;j<pixels;j++)
    {
      ARMword pixel = (i>>(j*srcbpp)) & ((1<<srcbpp)-1);
      pixel = pixel*mul;
      out |= pixel<<(j*destbpp);
    }
    *dest++ = out;
  }
}

/*

  Routine to copy a bitstream from one bit-aligned location to another, while
  expanding the data via a lookup table

  dest & src pointers must be ARMword aligned
  destalign & srcalign must be 0 <= x < 32
  count = available source data, in bits, multiple of srcbpp
  srcbpp = src BPP (1,2,4,8)
  factor = log2 of how much bigger each dest pixel is (1 <= factor <= log2(32/srcbpp))
  destalign must be multiple of srcbpp<<factor
*/

void BitCopyExpand(ARMword *dest,int destalign,const ARMword *src,int srcalign,int count,const ARMword *expandtable,unsigned int srcbpp,unsigned int factor)
{
  unsigned int destbpp = srcbpp<<factor;
  unsigned int pixels = 1;
  while((destbpp*pixels < 32) && (srcbpp*pixels < 8))
    pixels <<= 1;
  unsigned int srcbits = srcbpp*pixels;
  unsigned int destbits = destbpp*pixels;
  ARMword srcmask = (1<<srcbits)-1;
  /* Get the destination word aligned */
  int invdestalign = 32-destalign;
  int invsrcalign = 32-srcalign;
  ARMword tempsrc1 = *src++;
  ARMword tempsrc2 = *src++;
  if(destalign)
  {
    ARMword tempdest = *dest;
    ARMword destmask = (count<=invdestalign?(1<<count):0)-1;
    destmask = destmask<<destalign;
    tempdest &= ~destmask;
    ARMword tempsrc3 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    while(destalign < 32)
    {
      ARMword in = expandtable[tempsrc3 & ((1<<srcbpp)-1)]; /* One pixel at a time for simplicity */
      tempdest |= in<<destalign;
      srcalign += srcbpp;
      destalign += destbpp;
      tempsrc3 >>= srcbpp;
      count -= srcbpp;
    }
    *dest++ = tempdest;
    if(srcalign >= 32)
    {
      srcalign &= 0x1f;
      tempsrc1 = tempsrc2;
      tempsrc2 = *src++;
    }
    invsrcalign = 32-srcalign;
  }
  if(destbits < 32)
  {
    /* Table isn't big enough for us to get one full word per entry */
    ARMword tempsrc3 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    int remaining = 32;
    ARMword tempdest = 0;
    int outshift = 0;
    /* There will be (count<<factor)>>5 full words available, and we want to stop when there's less than one full word left */
    int bitsleft = (count<<factor)&0x1f; 
    while(count > bitsleft)
    {
      tempdest |= expandtable[tempsrc3 & srcmask]<<outshift;
      outshift += destbits;
      if(outshift >= 32)
      {
        *dest++ = tempdest;
        tempdest = 0;
        outshift = 0;
      }
      tempsrc3 >>= srcbits;
      remaining -= srcbits;
      count -= srcbits;
      srcalign += srcbits;
      if(!remaining)
      {
        if(srcalign >= 32)
        {
          srcalign &= 0x1f;
          tempsrc1 = tempsrc2;
          tempsrc2 = *src++;
        }
        invsrcalign = 32-srcalign;
        tempsrc3 = (tempsrc1>>srcalign);
        if(srcalign)
          tempsrc3 |= (tempsrc2<<invsrcalign);
        remaining = 32;
      }
    }
  }
  else
  {
    /* One output word for every srcbits */
    ARMword tempsrc3 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    int remaining = 32;
    while(count >= srcbits)
    {
      *dest++ = expandtable[tempsrc3 & srcmask];
      tempsrc3 >>= srcbits;
      remaining -= srcbits;
      count -= srcbits;
      srcalign += srcbits;
      if(!remaining)
      {
        if(srcalign >= 32)
        {
          srcalign &= 0x1f;
          tempsrc1 = tempsrc2;
          tempsrc2 = *src++;
        }
        invsrcalign = 32-srcalign;
        tempsrc3 = (tempsrc1>>srcalign);
        if(srcalign)
          tempsrc3 |= (tempsrc2<<invsrcalign);
        remaining = 32;
      }
    }
  }
  if(count)
  {
    /* End bits */
    ARMword tempdest = *dest;
    ARMword destmask = 0xffffffff<<(count<<factor);
    tempdest &= destmask;
    ARMword tempsrc3 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    int outshift = 0;
    while(count)
    {    
      ARMword in = expandtable[tempsrc3 & ((1<<srcbpp)-1)]; /* One pixel at a time for simplicity */
      tempdest |= in<<outshift;
      srcalign += srcbpp;
      outshift += destbpp;
      tempsrc3 >>= srcbpp;
      count -= srcbpp;
    }
    *dest = tempdest;
  }
}
