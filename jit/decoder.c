#include "decoder.h"

void Decoder_Decode(Instruction *out, uint32_t instr)
{
  out->instr = instr;
  if (instr >= 0xF0000000)
  {
    /* NV */
    out->type = InstrType_NOP;
    return;
  }
  switch ((instr >> 24) & 0xf)
  {
  case 0x0:
    if ((instr & 0x0fc000f0) == 0x90) {
      out->type = InstrType_Multiply;
      break;
    } 
  case 0x1:
  case 0x2:
  case 0x3:
    if (Decoder_DataProc_IsCompare(out) && !Decoder_DataProc_SFlag(out)) {
      /* Detect MRS, MSR, etc. at this decode phase */
      /* XXX cycle counting will be wrong here? */
      out->type = InstrType_NOP;
      break;
    }
    /* Non-immediate with bit 7 & 4 set isn't valid */
    if (!Decoder_DataProc_ImmFlag(out) && ((instr & 0x90) == 0x90)) {
      out->type = InstrType_Other;
      break;
    }
    out->type = InstrType_DataProc;
    break;
  case 0x4:
  case 0x5:
  case 0x6:
  case 0x7:
    out->type = InstrType_LDRSTR;
    break;
  case 0x8:
  case 0x9:
    out->type = InstrType_LDMSTM;
    break;
  case 0xa:
  case 0xb:
    out->type = InstrType_Branch;
    break;
  case 0xc: /* LDC/STC */
  case 0xd: /* LDC/STC */
  case 0xe: /* CDP */
    out->type = InstrType_Other;
  case 0xf:
    out->type = InstrType_SWI;
    break;
  /* TODO: SWP */
  }
}

int Decoder_LDMSTM_NumRegs(const Instruction *instr)
{
  int i,regs = 0;
  for(i=0;i<16;i++) {
    if (instr->instr & (1<<i)) {
      regs++;
    }
  }
  return regs;
}

int Decoder_LDMSTM_Cycles(const Instruction *instr)
{
  /* XXX fix this to be correct for all cases */
  return Decoder_LDMSTM_NumRegs(instr) + 2;
}
