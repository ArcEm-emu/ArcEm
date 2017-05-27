#ifndef DECODER_HEADER
#define DECODER_HEADER

#include "emuinterf.h"

/* ARMv2 instruction decoder */

#define JIT_EQ (0u<<28)
#define JIT_NE (1u<<28)
#define JIT_CS (2u<<28)
#define JIT_CC (3u<<28)
#define JIT_MI (4u<<28)
#define JIT_PL (5u<<28)
#define JIT_VS (6u<<28)
#define JIT_VC (7u<<28)
#define JIT_HI (8u<<28)
#define JIT_LS (9u<<28)
#define JIT_GE (10u<<28)
#define JIT_LT (11u<<28)
#define JIT_GT (12u<<28)
#define JIT_LE (13u<<28)
#define JIT_AL (14u<<28)
#define JIT_NV (15u<<28)

#define JIT_AND (0u<<20)
#define JIT_EOR (2u<<20)
#define JIT_SUB (4u<<20)
#define JIT_RSB (6u<<20)
#define JIT_ADD (8u<<20)
#define JIT_ADC (10u<<20)
#define JIT_SBC (12u<<20)
#define JIT_RSC (14u<<20)
#define JIT_TST (16u<<20)
#define JIT_TEQ (18u<<20)
#define JIT_CMP (20u<<20)
#define JIT_CMN (22u<<20)
#define JIT_ORR (24u<<20)
#define JIT_MOV (26u<<20)
#define JIT_BIC (28u<<20)
#define JIT_MVN (30u<<20)

#define JIT_S (1<<20)

typedef enum {
  InstrType_NOP,
  InstrType_Branch,
  InstrType_DataProc,
  InstrType_Multiply,
  InstrType_LDRSTR,
  InstrType_LDMSTM,
  InstrType_SWI,
  InstrType_Other,

  InstrType_Count,
} InstrType;

typedef struct {
  InstrType type;
  uint32_t instr;
} Instruction;

extern void Decoder_Decode(Instruction *out, uint32_t instr);

static inline uint32_t Decoder_CC(const Instruction *instr)
{
  return instr->instr & 0xF0000000;
}

static inline uint32_t Decoder_Conditional(const Instruction *instr)
{
  return instr->instr < JIT_AL;
}

static inline uint32_t Decoder_Branch_BLFlag(const Instruction *instr)
{
  return instr->instr & (1<<24);
}

static inline int32_t Decoder_Branch_Offset(const Instruction *instr)
{
  return (((int32_t) (instr->instr << 8)) >> 6) + 8;
}

/* =0 if op2 is a (shifted) register */
static inline uint32_t Decoder_DataProc_ImmFlag(const Instruction *instr)
{
  return instr->instr & (1<<25);
}

static inline uint32_t Decoder_DataProc_SFlag(const Instruction *instr)
{
  return instr->instr & (1<<20);
}

static inline bool Decoder_DataProc_IsShiftedReg(const Instruction *instr)
{
  return !Decoder_DataProc_ImmFlag(instr) && ((instr->instr & 0x90) == 0x10);
}

static inline uint32_t Decoder_DataProc_Op(const Instruction *instr)
{
  return instr->instr & (15<<21);
}

static inline bool Decoder_DataProc_IsCompare(const Instruction *instr)
{
  return (Decoder_DataProc_Op(instr)>>23) == 2;
}

static inline bool Decoder_DataProc_UsesRn(const Instruction *instr)
{
  /* MOV, MVN don't use Rn */
  uint32_t op = Decoder_DataProc_Op(instr);
  return (op != JIT_MOV) && (op != JIT_MVN);
}

/* Non-compare instructions always use Rd
   Compare instructions which have Rd==15 use Rd (P suffix) */
static inline bool Decoder_DataProc_UsesRd(const Instruction *instr)
{
  return (((instr->instr>>12)&15) == 15) || !Decoder_DataProc_IsCompare(instr);
}

/* Returns true if the C flag is used as an input of the ALU */
static inline bool Decoder_DataProc_CarryIn(const Instruction *instr)
{
  /* ADC, SBC, RSC use carry */
  uint32_t op = Decoder_DataProc_Op(instr);
  if ((op == JIT_ADC) || (op == JIT_SBC) || (op == JIT_RSC)) {
    return true;
  }
  /* RRX uses carry */
  if (Decoder_DataProc_ImmFlag(instr)) {
    return false;
  }
  return ((instr->instr & 0xff0) == 0x060);
}

/* =0 if MUL, else MLA */
static inline uint32_t Decoder_Multiply_AccumFlag(const Instruction *instr)
{
  return instr->instr & (1<<21);
}

static inline uint32_t Decoder_Multiply_SFlag(const Instruction *instr)
{
  return instr->instr & (1<<20);
}

/* =0 if store */
static inline uint32_t Decoder_LDRSTR_LoadFlag(const Instruction *instr)
{
  return instr->instr & (1<<20);
}

/* !=0 if pre-indexed */
static inline uint32_t Decoder_LDRSTR_PreFlag(const Instruction *instr)
{
  return instr->instr & (1<<24);
}

/* true if writeback */
static inline bool Decoder_LDRSTR_WritebackFlag(const Instruction *instr)
{
  return !Decoder_LDRSTR_PreFlag(instr) || (instr->instr & (1<<21));
}

/* true if T flag */
static inline bool Decoder_LDRSTR_TFlag(const Instruction *instr)
{
  return !Decoder_LDRSTR_PreFlag(instr) && (instr->instr & (1<<21));
}

/* !=0 if byte */
static inline uint32_t Decoder_LDRSTR_ByteFlag(const Instruction *instr)
{
  return instr->instr & (1<<22);
}

/* !=0 if up */
static inline uint32_t Decoder_LDRSTR_UpFlag(const Instruction *instr)
{
  return instr->instr & (1<<23);
}

/* =0 if op2 is a (shifted) register */
static inline bool Decoder_LDRSTR_ImmFlag(const Instruction *instr)
{
  return !(instr->instr & (1<<25));
}

/* true if offset is (probably) non-zero */
static inline bool Decoder_LDRSTR_HasOffset(const Instruction *instr)
{
  return !Decoder_LDRSTR_ImmFlag(instr) || (instr->instr & 0xfff);
}

/* Returns true if the C flag is used as an input of the ALU */
static inline bool Decoder_LDRSTR_CarryIn(const Instruction *instr)
{
  /* RRX uses carry */
  if (Decoder_LDRSTR_ImmFlag(instr)) {
    return false;
  }
  return ((instr->instr & 0xfe0) == 0x060);
}

/* =0 if store */
static inline uint32_t Decoder_LDMSTM_LoadFlag(const Instruction *instr)
{
  return instr->instr & (1<<20);
}
 
/* !=0 if writeback */
static inline uint32_t Decoder_LDMSTM_WritebackFlag(const Instruction *instr)
{
  return instr->instr & (1<<21);
}

/* !=0 if ^ */
static inline uint32_t Decoder_LDMSTM_HatFlag(const Instruction *instr)
{
  return instr->instr & (1<<22);
}

/* !=0 if up */
static inline uint32_t Decoder_LDMSTM_UpFlag(const Instruction *instr)
{
  return instr->instr & (1<<23);
}

/* !=0 if pre-indexed */
static inline uint32_t Decoder_LDMSTM_PreFlag(const Instruction *instr)
{
  return instr->instr & (1<<24);
}

extern int Decoder_LDMSTM_NumRegs(const Instruction *instr);

extern int Decoder_LDMSTM_Cycles(const Instruction *instr);

/* Validity: DataProc && UsesRn
             Multiply && AccumFlag
             LDRSTR
             LDMSTM */
static inline uint32_t Decoder_Rn(const Instruction *instr)
{
  return (instr->instr >> 16) & 0xf;
}

/* Validity: DataProc && UsesRd
             Multiply
             LDRSTR */
static inline uint32_t Decoder_Rd(const Instruction *instr)
{
  return (instr->instr >> 12) & 0xf;
}

/* Validity: DataProc && !ImmFlag
             Multiply
             LDRSTR && !ImmFlag */
static inline uint32_t Decoder_Rm(const Instruction *instr)
{
  return instr->instr & 0xf;
}

/* Validity: DataProc && IsShiftedReg
             Multiply */
static inline uint32_t Decoder_Rs(const Instruction *instr)
{
  return (instr->instr >> 8) & 0xf;
}

#endif
