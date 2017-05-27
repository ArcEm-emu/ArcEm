#ifndef CODEBLOCKS_HEADER
#define CODEBLOCKS_HEADER

#include <stdio.h>
#include "dirtyranges.h"
#include "jitpage.h"

/* ForwardCodeBlock writes code going forwards, BackwardCodeBlock writes code going backwards */
typedef struct {
  DirtyRange *dirty;
  uintptr_t nextinstr; /* Address next instruction will be written to */
  uintptr_t data_start; /* Start of data section, inclusive */
  uintptr_t data_end; /* End of data section, exclusive */
} ForwardCodeBlock, BackwardCodeBlock;

/* Get next ID that will be allocated; marks start of a code generation pass */
extern int CodeBlock_NextID(void);

/* Claim the ID that was returned by NextID; marks end of a code generation pass */
extern void CodeBlock_ClaimID(int id, JITPage *page);

/* Mark the chain starting with 'ID' as invalid */
extern void CodeBlock_InvalidateID(int id);

extern void ForwardCodeBlock_New(ForwardCodeBlock *out, bool chain);
extern void BackwardCodeBlock_New(BackwardCodeBlock *out, bool chain);

static inline uintptr_t ForwardCodeBlock_WriteCode(ForwardCodeBlock *block, uint32_t word)
{
  uint32_t *ptr = (uint32_t *) block->nextinstr;
#ifdef JIT_DEBUG
  fprintf(stderr,"%08x: %08x\n",ptr,word);
#endif
  *ptr++ = word;
  block->dirty->end = block->nextinstr = (uintptr_t) ptr;
  if (block->data_start == (uintptr_t) (ptr+1))
  {
    /* Ran out of space in this block, generate branch to new block */
    ForwardCodeBlock_New(block, true);
  }
  return (uintptr_t) (ptr-1);
}


static inline uintptr_t ForwardCodeBlock_WriteData(ForwardCodeBlock *block, uint32_t data)
{
  /* TODO reuse existing values */
  uint32_t *ptr = (uint32_t *) block->data_start;
  *(--ptr) = data;
  block->data_start = (uintptr_t) ptr;
  if ((uintptr_t) ptr == block->nextinstr+4)
  {
    /* Ran out of space in this block, generate branch to new block */
    ForwardCodeBlock_New(block, true);    
  }
  return (uintptr_t) ptr;
}

static inline uintptr_t BackwardCodeBlock_WriteCode(BackwardCodeBlock *block, uint32_t word)
{
  uint32_t *ptr = (uint32_t *) block->nextinstr;
  block->dirty->start = (uintptr_t) ptr;
  *ptr-- = word;
  block->nextinstr = (uintptr_t) ptr;
  if (block->data_end == (uintptr_t) ptr)
  {
    /* Ran out of space in this block, generate branch from new block */
    BackwardCodeBlock_New(block, true);
  }
  return (uintptr_t) (ptr+1);
}

static inline uintptr_t BackwardCodeBlock_WriteData(BackwardCodeBlock *block, uint32_t data)
{
  /* TODO reuse existing values */
  uint32_t *ptr = (uint32_t *) block->data_end;
  *(++ptr) = data;
  block->data_end = (uintptr_t) ptr;
  if ((uintptr_t) ptr == block->nextinstr)
  {
    /* Ran out of space in this block, generate branch from new block */
    BackwardCodeBlock_New(block, true);
  }
  return (uintptr_t) (ptr-1);
}

#endif
