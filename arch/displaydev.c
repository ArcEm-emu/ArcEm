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
#include "armemu.h"

const DisplayDev *DisplayDev_Current = NULL;

bool DisplayDev_UseUpdateFlags = true;
bool DisplayDev_AutoUpdateFlags = false;
int DisplayDev_FrameSkip = 0;

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
  static const int_fast8_t offsets[4] = {19-6,11-6,7-6,5-6};
  *x = VIDC.Horiz_CursorStart-(VIDC.Horiz_DisplayStart*2+offsets[(VIDC.ControlReg & 0xc)>>2]);
  *y = VIDC.Vert_CursorStart-VIDC.Vert_DisplayStart;
}

static const uint32_t vidcclocks[4] = {24000000,25175000,36000000,24000000};

uint32_t DisplayDev_GetVIDCClockIn(void)
{
  return vidcclocks[ioc.IOEBControlReg & IOEB_CR_VIDC_MASK];
}

void DisplayDev_VSync(ARMul_State *state)
{
  /* Trigger VSync */
  ioc.IRQStatus|=IRQA_VFLYBK;
  IO_UpdateNirq(state);
  /* Update ARMul_EmuRate */
  EmuRate_Update(state);
}


/*

  Endian swapping

  Note that ArcEm stores each ARMword of memory in host endianness. Manual
  endian swapping is only performed on the addresses used for byte reads/writes.

  This means that when we load an ARMword of memory containing pixels '00',
  '11', '22', '33', we get the value 0x33221100, the same as if we were on a
  little-endian system. This means the only endian swapping that's required is
  when we read/write to the host display buffer.

*/

#ifdef HOST_BIGENDIAN
/*

  Routine to copy bytes from one byte-aligned location to another

  Note that the byte alignment of 'src' is wrong, because src points to
  little-endian data instead of big-endian. So to reduce the chance of
  confusion, this routine works by endian swapping the source data/addresses
  (instead of just endian swapping the destination, as suggested above)

*/
void ByteCopy(uint8_t *dest,const uint8_t *src,size_t size)
{
  if(!size)
    return;
  /* Align source */
  int srcalign = ((int) src) & 3;
  if(srcalign)
  {
    src -= srcalign;
    srcalign ^= 3; /* Byte swap the source address offset */
    while((srcalign+1) && size)
    {
      *dest++ = src[srcalign];
      srcalign--;
      size--;
    }
    src += 4;
  }    
  /* Examine alignment */
  int destalign = ((int) dest) & 3;
  /* Word aligned? */
  if(!destalign && (size >= 4))
  {
    EndianWordCpy((ARMword *) dest,(const ARMword *) src,size>>2);
    dest += (size & ~3);
    src += (size & ~3);
    size &= 3;
  }
  else
  {
    /* Source is word aligned, but destination isn't. Copy data one byte at a time. */
    while(size >= 4)
    {
      dest[0] = src[3];
      dest[1] = src[2];
      dest[2] = src[1];
      dest[3] = src[0];
      dest += 4;
      src += 4;
      size -= 4;
    }
  }
  /* Any remaining bytes? */
  while(size--)
  {
    dest[size] = src[size ^ 3];
  }
}

/*

  Routine to copy bytes from one byte-aligned location to another

  This time we need to do endian swapping on the destination address

*/
void InvByteCopy(uint8_t *dest,const uint8_t *src,size_t size)
{
  if(!size)
    return;
  /* Align destination */
  int destalign = ((int) dest) & 3;
  if(destalign)
  {
    dest -= destalign;
    destalign ^= 3; /* Byte swap the dest address offset */
    while((destalign+1) && size)
    {
      dest[destalign] = *src++;
      destalign--;
      size--;
    }
    dest += 4;
  }    
  /* Examine alignment */
  int srcalign = ((int) src) & 3;
  /* Word aligned? */
  if(!srcalign && (size >= 4))
  {
    EndianWordCpy((ARMword *) dest,(const ARMword *) src,size>>2);
    dest += (size & ~3);
    src += (size & ~3);
    size &= 3;
  }
  else
  {
    /* Destination is word aligned, but source isn't. Copy data one byte at a time. */
    while(size >= 4)
    {
      dest[0] = src[3];
      dest[1] = src[2];
      dest[2] = src[1];
      dest[3] = src[0];
      dest += 4;
      src += 4;
      size -= 4;
    }
  }
  /* Any remaining bytes? */
  while(size--)
  {
    dest[size ^ 3] = src[size];
  }
}
#endif

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
    ARMword tempsrc3, tempdest = EndianSwap(*dest);

    ARMword mask = (count<=invdestalign?(1<<count):0)-1;
    mask = mask<<destalign;
    tempdest &= ~mask;
    tempsrc3 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    tempdest |= (tempsrc1<<destalign) & mask;
    *dest++ = EndianSwap(tempdest);

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
    EndianWordCpy(dest,src-2,temp);
    dest += temp;
    tempsrc1 = src[temp-2];
    count &= 0x1f;
  }
  else while(count >= 32)
  {
    *dest++ = EndianSwap((tempsrc1>>srcalign) | (tempsrc2<<invsrcalign));
    tempsrc1 = tempsrc2;
    tempsrc2 = *src++;
    count -= 32;
  }
  if(count)
  {
    /* End bits */
    ARMword tempdest = EndianSwap(*dest);
    ARMword mask = 0xffffffff<<count;
    tempdest &= mask;
    tempsrc1 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc1 |= (tempsrc2<<invsrcalign);
    tempdest |= tempsrc1 &~ mask;
    *dest = EndianSwap(tempdest);
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
  return 1<<(srcbpp*pixels);
}

/*

  Routine to generate lookup tables for fast upscaling/expansion

  dest = destination buffer, must be appropriate size
  srcbpp = src BPP (1,2,4,8)
  factor = log2 of how much bigger each dest pixel is (1 <= factor <= log2(32/srcbpp))
  mul = multiplier (0x1 for simple BPP conversion, 0x11 for 4-bit 2X upscale, 0x101 for 8-bit 2X upscale, etc.)

*/

void GenExpandTable(ARMword *dest,unsigned int srcbpp,unsigned int factor,ARMword mul)
{
  ARMword i,j;
  unsigned int srcbits, destbpp = srcbpp<<factor;
  unsigned int pixels = 1;
  while((destbpp*pixels < 32) && (srcbpp*pixels < 8))
    pixels <<= 1;
  srcbits = 1<<(srcbpp*pixels);
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
  int invdestalign, invsrcalign; 
  ARMword srcmask, tempsrc1, tempsrc2;
  unsigned int srcbits, destbits;
  unsigned int destbpp = srcbpp<<factor;
  unsigned int pixels = 1;
  while((destbpp*pixels < 32) && (srcbpp*pixels < 8))
    pixels <<= 1;
  srcbits = srcbpp*pixels;
  destbits = destbpp*pixels;
  srcmask = (1<<srcbits)-1;
  /* Get the destination word aligned */
  invdestalign = 32-destalign;
  invsrcalign = 32-srcalign;
  tempsrc1 = *src++;
  tempsrc2 = *src++;
  if(destalign)
  {
    ARMword tempsrc3, tempdest = EndianSwap(*dest);
    ARMword destmask = (count<=invdestalign?(1<<count):0)-1;
    destmask = destmask<<destalign;
    tempdest &= ~destmask;
    tempsrc3 = (tempsrc1>>srcalign);
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
    *dest++ = EndianSwap(tempdest);
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
    int remaining = 32;
    ARMword tempdest = 0;
    int outshift = 0;
    /* There will be (count<<factor)>>5 full words available, and we want to stop when there's less than one full word left */
    int bitsleft = (count<<factor)&0x1f; 
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    while(count > bitsleft)
    {
      tempdest |= expandtable[tempsrc3 & srcmask]<<outshift;
      outshift += destbits;
      if(outshift >= 32)
      {
        *dest++ = EndianSwap(tempdest);
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
    int remaining = 32;
    /* One output word for every srcbits */
    ARMword tempsrc3 = (tempsrc1>>srcalign);
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    while(count >= (int)srcbits)
    {
      *dest++ = EndianSwap(expandtable[tempsrc3 & srcmask]);
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
    ARMword tempdest = EndianSwap(*dest);
    ARMword destmask = 0xffffffff<<(count<<factor);
    ARMword tempsrc3 = (tempsrc1>>srcalign);
    int outshift = 0;
    tempdest &= destmask;
    if(srcalign)
      tempsrc3 |= (tempsrc2<<invsrcalign);
    while(count)
    {    
      ARMword in = expandtable[tempsrc3 & ((1<<srcbpp)-1)]; /* One pixel at a time for simplicity */
      tempdest |= in<<outshift;
      srcalign += srcbpp;
      outshift += destbpp;
      tempsrc3 >>= srcbpp;
      count -= srcbpp;
    }
    *dest = EndianSwap(tempdest);
  }
}
