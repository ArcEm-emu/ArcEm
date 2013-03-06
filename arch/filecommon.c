/*
  arch/filecommon.c

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Common file handling routines

  Part of Arcem released under the GNU GPL, see file COPYING
  for details.

*/

#include <stdio.h>
#include <stddef.h>

#include "armarc.h"
#include "filecalls.h"
#include "displaydev.h"
#include "ControlPane.h"

#define USE_FILEBUFFER

/* Note: Musn't be used as a parameter to ReadEmu/WriteEmu! */
static ARMword temp_buf_word[32768/4];
static uint8_t *temp_buf = (uint8_t *) temp_buf_word;

#ifdef USE_FILEBUFFER
/* File buffering */

#define MAX_FILEBUFFER (1024*1024)
#define MIN_FILEBUFFER (32768)

static uint8_t *buffer=0;
static size_t buffer_size=0;

static void ensure_buffer_size(size_t buffer_size_needed)
{
  if (buffer_size_needed > buffer_size) {
    buffer = realloc(buffer, buffer_size_needed);
    if (!buffer) {
      ControlPane_Error(EXIT_FAILURE,"filecommon could not increase buffer size to %u bytes\n",
              (ARMword) buffer_size_needed);
    }
    buffer_size = buffer_size_needed;
  }
}

bool filebuffer_inuse = false;
FILE *filebuffer_file = NULL;
size_t filebuffer_remain = 0; /* Reads: Total amount left to buffer. Writes: Total amount the user said he was going to write */
size_t filebuffer_buffered = 0; /* Reads: How much is currently in the buffer. Writes: Total amount collected by filebuffer_write() */
size_t filebuffer_offset = 0; /* Reads/writes: Current offset within buffer */

static void filebuffer_fill(void)
{
  size_t temp = MIN(filebuffer_remain,MAX_FILEBUFFER);
  filebuffer_offset = 0;
  filebuffer_buffered = fread(buffer,1,temp,filebuffer_file);
  if(filebuffer_buffered != temp)
    filebuffer_remain = 0;
  else
    filebuffer_remain -= filebuffer_buffered;
}

static void filebuffer_initread(FILE *pFile,size_t uCount)
{
  size_t temp;
  filebuffer_inuse = false;
  filebuffer_file = pFile;
  if(uCount <= MIN_FILEBUFFER)
    return;
  filebuffer_inuse = true;
  temp = MIN(uCount,MAX_FILEBUFFER);
  ensure_buffer_size(temp);
  filebuffer_remain = uCount;
  filebuffer_fill();
}

static size_t filebuffer_read(uint8_t *pBuffer,size_t uCount,bool endian)
{
  size_t ret, avail;
  if(!filebuffer_inuse)
  {
    if(endian)
      return File_ReadEmu(filebuffer_file,pBuffer,uCount);
    else
      return fread(pBuffer,1,uCount,filebuffer_file);
  }
  ret = 0;
  while(uCount)
  {
    if(filebuffer_buffered == filebuffer_offset)
    {
      if(!filebuffer_remain)
        return ret;
      filebuffer_fill();
    }
    avail = MIN(uCount,filebuffer_buffered-filebuffer_offset);
    if(endian)
      InvByteCopy(pBuffer,buffer+filebuffer_offset,avail);
    else
      memcpy(pBuffer,buffer+filebuffer_offset,avail);
    filebuffer_offset += avail;
    ret += avail;
    pBuffer += avail;
    uCount -= avail;
  }
  return ret;
}

static void filebuffer_initwrite(FILE *pFile,size_t uCount)
{
  size_t temp;
  filebuffer_inuse = false;
  filebuffer_file = pFile;
  filebuffer_remain = uCount; /* Actually treated as total writeable */
  filebuffer_buffered = 0;
  if(uCount <= MIN_FILEBUFFER)
    return;
  filebuffer_inuse = true;
  temp = MIN(uCount,MAX_FILEBUFFER);
  ensure_buffer_size(temp);
  filebuffer_offset = 0;
}

static void filebuffer_write(uint8_t *pBuffer,size_t uCount,bool endian)
{
  if(filebuffer_remain == filebuffer_buffered)
    return;
  if(!filebuffer_inuse)
  {
    size_t temp;
    uCount = MIN(uCount,filebuffer_remain-filebuffer_buffered);
    if(endian)
      temp = File_WriteEmu(filebuffer_file,pBuffer,uCount);
    else
      temp = fwrite(pBuffer,1,uCount,filebuffer_file);
    filebuffer_buffered += temp;
    if(temp != uCount)
      filebuffer_remain = filebuffer_buffered;
     return;
  }
  while(uCount)
  {
    size_t temp = MIN(uCount,buffer_size-filebuffer_offset);
    if(endian)
      ByteCopy(buffer+filebuffer_offset,pBuffer,temp);
    else
      memcpy(buffer+filebuffer_offset,pBuffer,temp);
    filebuffer_offset += temp;
    uCount -= temp;
    pBuffer += temp;
    if(filebuffer_offset == buffer_size)
    {
      /* Flush */
      size_t temp2 = fwrite(buffer,1,filebuffer_offset,filebuffer_file);
      filebuffer_buffered += temp2;
      if(temp2 != filebuffer_offset)
      {
        filebuffer_remain = filebuffer_buffered;
        return;
      }
      filebuffer_offset = 0;
    }
  }
}

static size_t filebuffer_endwrite(void)
{
  if(filebuffer_inuse && filebuffer_offset)
  {
    /* Flush */
    size_t temp2 = fwrite(buffer,1,filebuffer_offset,filebuffer_file);
    filebuffer_buffered += temp2;
  }
  return filebuffer_buffered;
}
#endif

/**
 * File_ReadEmu
 *
 * Reads from the given file handle into the given buffer
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to read from
 * @param pBuffer Buffer to write to
 * @param uCount Number of bytes to read
 * @returns Number of bytes read
 */
size_t File_ReadEmu(FILE *pFile,uint8_t *pBuffer,size_t uCount)
{
#ifdef HOST_BIGENDIAN
  /* The only way to safely read the data without running the risk of corrupting
     the destination buffer is to read into the temp buffer. (This is because
     the data will need endian swapping after reading, but we can't guarantee
     that we'll read all uCount bytes, so we can't easily pre-swap the buffer
     to avoid losing the original contents of the first/last words) */
  size_t ret = 0;
  while(uCount > 0)
  {
    int offset = ((int) pBuffer)&3;
    size_t count2 = MIN(sizeof(temp_buf)-offset,uCount);
    size_t read = fread(temp_buf+offset,1,count2,pFile);
    InvByteCopy(pBuffer,temp_buf+offset,read);
    ret += read;
    pBuffer += read;
    uCount -= read;

    if(read < count2)
      break;
  }
  return ret;
#else
  return fread(pBuffer,1,uCount,pFile);
#endif
}

/**
 * File_WriteEmu
 *
 * Writes data to the given file handle
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to write to
 * @param pBuffer Buffer to read from
 * @param uCount Number of bytes to write
 * @returns Number of bytes written
 */
size_t File_WriteEmu(FILE *pFile,const uint8_t *pBuffer,size_t uCount)
{
#ifdef HOST_BIGENDIAN
  /* Split into chunks and copy into the temp buffer */
  size_t ret = 0;
  while(uCount > 0)
  {
    int offset = ((int) pBuffer)&3;
    size_t count2 = MIN(sizeof(temp_buf)-offset,uCount);
    ByteCopy(temp_buf+offset,pBuffer,count2);
    size_t written = fwrite(temp_buf+offset,1,count2,pFile);
    ret += written;
    pBuffer += written;
    uCount -= written;
    if(written < count2)
      break;
  }
  return ret; 
#else
  return fwrite(pBuffer,1,uCount,pFile);
#endif
}

/**
 * File_ReadRAM
 *
 * Reads from the given file handle into emulator memory
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to read from
 * @param uAddress Logical address of buffer to write to
 * @param uCount Number of bytes to read
 * @returns Number of bytes read
 */
size_t File_ReadRAM(ARMul_State *state, FILE *pFile,ARMword uAddress,size_t uCount)
{
  size_t ret = 0;
#ifdef USE_FILEBUFFER
  filebuffer_initread(pFile,uCount);
#endif

  while(uCount > 0)
  {
    FastMapEntry *entry;
    FastMapRes res;
    size_t amt = MIN(4096-(uAddress&4095),uCount);

    uAddress &= UINT32_C(0x3ffffff);

    entry = FastMap_GetEntryNoWrap(state,uAddress);
    res = FastMap_DecodeWrite(entry,state->FastMapMode);
    if(FASTMAP_RESULT_DIRECT(res))
    {
      size_t temp, temp2;
      ARMEmuFunc *func;
      uint8_t *phy = (uint8_t *) FastMap_Log2Phy(entry,uAddress);

      /* Scan ahead to work out the size of this memory block */
      while(amt < uCount)
      {
        uint8_t *phy2;
        ARMword uAddrNext = (uAddress+amt) & UINT32_C(0x3ffffff);
        size_t amt2 = MIN(4096,uCount-amt);
        FastMapEntry *entry2 = FastMap_GetEntryNoWrap(state,uAddrNext);
        FastMapRes res2 = FastMap_DecodeRead(entry2,state->FastMapMode);
        if(!FASTMAP_RESULT_DIRECT(res2))
          break;
        phy2 = (uint8_t *) FastMap_Log2Phy(entry2,uAddrNext);
        if(phy2 != phy+amt)
          break;
        /* These fastmap pages are contiguous in physical memory */
        amt += amt2;
      }

#ifdef USE_FILEBUFFER
      temp = filebuffer_read(phy,amt,true);
#else
      temp = File_ReadEmu(pFile,phy,amt);
#endif

      /* Clobber emu funcs for that region */
      temp2 = (temp+(uAddress&3)+3)&~UINT32_C(3);
      func = FastMap_Phy2Func(state,(ARMword *) (phy-(uAddress&3)));
      while(temp2>0)
      {
        *func++ = FASTMAP_CLOBBEREDFUNC;
        temp2-=4;
      }

      /* Update state */
      ret += temp;
      if(temp != amt)
        break;
      uAddress += temp;
      uCount -= temp;
    }
    else if(FASTMAP_RESULT_FUNC(res))
    {
      ARMword *w;
      /* Read into temp buffer */
#ifdef USE_FILEBUFFER
      size_t temp = filebuffer_read(temp_buf+(uAddress&3),amt,false);
#else
      size_t temp = fread(temp_buf+(uAddress&3),1,amt,pFile);
#endif
      size_t temp2 = temp;
      /* Deal with start bytes */
      uint8_t *c = temp_buf+(uAddress&3);
      while((uAddress&3) && temp2)
      {
        FastMap_StoreFunc(entry,state,uAddress,*c,FASTMAP_ACCESSFUNC_BYTE);
        uAddress++;
        temp2--;
        c++;
      }
      /* Deal with main body */
      w = (ARMword *) c;
      while(temp2 >= 4)
      {
        FastMap_StoreFunc(entry,state,uAddress,EndianSwap(*w),0);
        uAddress += 4;
        temp2 -= 4;
        w++;
        c += 4;
      }
      /* Deal with end bytes */
      while(temp2)
      {
        FastMap_StoreFunc(entry,state,uAddress,*c,FASTMAP_ACCESSFUNC_BYTE);
        uAddress++;
        temp2--;
        c++;
      }

      /* Update state */
      ret += temp;
      if(temp != amt)
        break;
      uCount -= temp;
    }
    else
    {
      /* Abort!
         Pretend as if the CPU accessed the memory */
      ARMul_DATAABORT(uAddress);
      break;
    }
  }
  return ret;
}

/**
 * File_WriteRAM
 *
 * Writes data from emulator memory to the given file handle
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to write to
 * @param uAddress Logical address of buffer to read from
 * @param uCount Number of bytes to write
 * @returns Number of bytes written
 */
size_t File_WriteRAM(ARMul_State *state, FILE *pFile,ARMword uAddress,size_t uCount)
{
#ifdef USE_FILEBUFFER
  filebuffer_initwrite(pFile,uCount);
#else
  size_t ret = 0;
#endif

  while(uCount > 0)
  {
    FastMapEntry *entry;
    FastMapRes res;
    size_t amt = MIN(4096-(uAddress&4095),uCount);

    uAddress &= UINT32_C(0x3ffffff);

    entry = FastMap_GetEntryNoWrap(state,uAddress);
    res = FastMap_DecodeRead(entry,state->FastMapMode);
    if(FASTMAP_RESULT_DIRECT(res))
    {
      uint8_t *phy = (uint8_t *) FastMap_Log2Phy(entry,uAddress);

      /* Scan ahead to work out the size of this memory block */
      while(amt < uCount)
      {
        uint8_t *phy2;
        ARMword uAddrNext = (uAddress+amt) & 0x3ffffff;
        size_t amt2 = MIN(4096,uCount-amt);
        FastMapEntry *entry2 = FastMap_GetEntryNoWrap(state,uAddrNext);
        FastMapRes res2 = FastMap_DecodeRead(entry2,state->FastMapMode);
        if(!FASTMAP_RESULT_DIRECT(res2))
          break;
        phy2 = (uint8_t *) FastMap_Log2Phy(entry2,uAddrNext);
        if(phy2 != phy+amt)
          break;
        /* These fastmap pages are contiguous in physical memory */
        amt += amt2;
      }        

#ifdef USE_FILEBUFFER
      filebuffer_write(phy,amt,true);
      /* Update state */
      uAddress += amt;
      uCount -= amt;
#else
      {
      size_t temp = File_WriteEmu(pFile,phy,amt);

      /* Update state */
      ret += temp;
      if(temp != amt)
        break;
      uAddress += temp;
      uCount -= temp;
      }
#endif
    }
    else if(FASTMAP_RESULT_FUNC(res))
    {
      /* Copy into temp buffer so we can perform endian swapping */
      uint8_t *temp = temp_buf+(uAddress&3);
      size_t temp2 = (amt+(uAddress&3)+3)&~UINT32_C(3); /* How many words to read */
      
      /* Copy the data a word at a time */
      ARMword *w = (ARMword *) temp_buf;
      uAddress &= ~UINT32_C(3);
      while(temp2 >= 4)
      {
        *w = EndianSwap(FastMap_LoadFunc(entry,state,uAddress));
        uAddress += 4;
        temp2 -= 4;
        w++;
      }

#ifdef USE_FILEBUFFER
      filebuffer_write(temp,amt,false);
      /* Update state */
      uCount -= amt;
#else
      /* Write it out */
      temp2 = fwrite(temp,1,amt,pFile);

      /* Update state */
      ret += temp2;
      if(temp2 != amt)
        break;
      uCount -= temp2;
#endif
    }
    else
    {
      /* Abort!
         Pretend as if the CPU accessed the memory */
      ARMul_DATAABORT(uAddress);
      break;
    }
  }
#ifdef USE_FILEBUFFER
  return filebuffer_endwrite();
#else
  return ret;
#endif
}
