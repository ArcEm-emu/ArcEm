#include "codeblocks.h"
#include "emuinterf.h"
#include "jitstate2.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
  JITPage *owner;
} codeblock_header;

#define CODEBLOCK_SIZE (512-sizeof(codeblock_header)) /* Size in bytes */
#define CODEBLOCK_COUNT 2048

typedef struct {
  codeblock_header header;
  uint8_t data[CODEBLOCK_SIZE];
} codeblock;

static codeblock codeblocks[CODEBLOCK_COUNT];
static int rr_next;

static codeblock *codeblock_claim(const JITState *jit)
{
  codeblock *block = &codeblocks[rr_next++];
  if (rr_next == CODEBLOCK_COUNT)
  {
    rr_next = 0;
  }
  if (block->header.owner)
  {
    JITPage_ForgetCode(jit,block->header.owner);
  }
  return block;
}

static void writebranch(uintptr_t src, uintptr_t dest)
{
  uint32_t offset = dest-8-src;
  *((uint32_t *) src) = ((offset>>2) & 0xffffff) | 0xEA000000;
}

int CodeBlock_NextID(void)
{
  return rr_next;
}

void CodeBlock_ClaimID(int id, JITPage *page)
{
  codeblocks[id].header.owner = page;
}

void CodeBlock_InvalidateID(int id)
{
  if (id != -1) {
    codeblocks[id].header.owner = NULL;
  }
}

void ForwardCodeBlock_New(ForwardCodeBlock *out, bool chain)
{
  const JITState *jit = JIT_GetState(JITEmuInterf_GetState());
  codeblock *block = codeblock_claim(jit);
#ifdef JIT_DEBUG
  fprintf(stderr,"ForwardCodeBlock_New: chain %d -> %08x\n",chain,block->data);
#endif
  /* If we're chaining blocks, insert a branch at the end of 'out' */
  if (chain)
  {
    uintptr_t loc = out->nextinstr;
    writebranch(loc, (uintptr_t) block->data);
    out->dirty->end = loc+4;
  }
  out->dirty = DirtyRanges_Claim((uintptr_t) block->data);
  out->nextinstr = (uintptr_t) block->data;
  out->data_start = out->data_end = (uintptr_t) (block->data + CODEBLOCK_SIZE);
}

void BackwardCodeBlock_New(BackwardCodeBlock *out, bool chain)
{
  const JITState *jit = JIT_GetState(JITEmuInterf_GetState());
  codeblock *block = codeblock_claim(jit);
  /* If we're chaining blocks, insert a branch at the end of 'block' */
  if (chain)
  {
    writebranch((uintptr_t) block->data, out->nextinstr+4);
  }
  out->dirty = DirtyRanges_Claim((uintptr_t) (block->data + CODEBLOCK_SIZE));
  if (chain)
  {
    out->dirty->start -= 4;
  }
  out->nextinstr = out->dirty->start - 4;
  out->data_start = out->data_end = (uintptr_t) block->data;
}
