#ifndef CODEGEN_HEADER
#define CODEGEN_HEADER

#include "emuinterf.h"
#include <assert.h>

static inline uint32_t JITCodeGen_LoadImm(uint32_t cc, uint32_t hostreg, uint32_t basereg, uint32_t offset)
{
  assert(offset < 4096);
  return cc | 0x05900000 | (basereg << 16) | (hostreg << 12) | offset;
}

static inline uint32_t JITCodeGen_Load_Rm(uint32_t cc, uint32_t hostreg, uint32_t basereg, uint32_t Rm)
{
  assert(Rm < 4096);
  return cc | 0x07900000 | (basereg << 16) | (hostreg << 12) | Rm;
}

static inline uint32_t JITCodeGen_StoreImm(uint32_t cc, uint32_t hostreg, uint32_t basereg, uint32_t offset)
{
  assert(offset < 4096);
  return cc | 0x05800000 | (basereg << 16) | (hostreg << 12) | offset;
}

static inline uint32_t JITCodeGen_EncodeImm12(uint32_t val)
{
  uint32_t ret = 0;
  if (val) {
    while (((val > 255) || !(val & 3)))
    {
      val = (val << 2) | (val >> 30);
      ret+=256;
      assert(ret != (16<<8));
    }
  }
  return ret | val;
}

static inline uint32_t JITCodeGen_DataProc_Rd_Imm(uint32_t cc, uint32_t op, uint32_t Rd, int imm)
{
  return cc | (1<<25) | op | (Rd<<12) | JITCodeGen_EncodeImm12(imm);
}

static inline uint32_t JITCodeGen_DataProc_Rd_Rn_Imm(uint32_t cc, uint32_t op, uint32_t Rd, uint32_t Rn, int imm)
{
  return cc | (1<<25) | op | (Rn<<16) | (Rd<<12) | JITCodeGen_EncodeImm12(imm);
}

static inline uint32_t JITCodeGen_DataProc_Rn_Imm(uint32_t cc, uint32_t op, uint32_t Rn, int imm)
{
  return cc | (1<<25) | op | (1<<20) | (Rn<<16) | JITCodeGen_EncodeImm12(imm);
}

static inline uint32_t JITCodeGen_DataProc_Rn_Rm(uint32_t cc, uint32_t op, uint32_t Rn, uint32_t Rm)
{
  return cc | op | (1<<20) | (Rn<<16) | Rm;
}

static inline uint32_t JITCodeGen_DataProc_Rd_Rm(uint32_t cc, uint32_t op, uint32_t Rd, uint32_t Rm)
{
  return cc | op | (Rd<<12) | Rm;
}

static inline uint32_t JITCodeGen_DataProc_Rd_Rn_Rm(uint32_t cc, uint32_t op, uint32_t Rd, uint32_t Rn, uint32_t Rm)
{
  return cc | op | (Rn<<16) | (Rd<<12) | Rm;
}

static inline uint32_t JITCodeGen_LoadNZCV(uint32_t cc, int hostreg)
{
  /* MSR CPSR_f,hostreg */
  return cc | 0x0128f000 | hostreg | cc;
}

static inline uint32_t JITCodeGen_SavePSR(uint32_t cc, int hostreg)
{
  /* MRS hostreg,CPSR */
  return cc | 0x010f0000 | (hostreg << 12) | cc;
}

static inline uint32_t JITCodeGen_Branch(uint32_t cc, int32_t offset)
{
  return cc | 0x0a000000 | (((offset-8)>>2) & 0xffffff);
}

#endif
