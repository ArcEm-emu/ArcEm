#ifndef JITPAGE_HEADER
#define JITPAGE_HEADER

#include "jitstate.h"

#define JITPAGE_SIZE 4096

struct JITPage {
  int codeblock; /* ID of first code block in chain, -1 if no code exists for this page */
  /* ??? do we need anything here? */
};

extern void JITPage_Init(JITState *jit,uint32_t romramchunksize);

/* Get the start address of this page */
static inline void *JITPage_StartOfPage(const JITState *jit,JITPage *page)
{
  int idx = page - jit->pages;
  return (void *) (jit->romramchunk + idx*JITPAGE_SIZE);  
}

/* Forget any generated code for this page (but retain memory attributes) */
extern void JITPage_ForgetCode(const JITState *jit,JITPage *page);

/* Get the page that contains this address */
static inline JITPage *JITPage_Get(const JITState *jit,void *addr)
{
  uint32_t idx = (((uintptr_t) addr) - jit->romramchunk)/JITPAGE_SIZE;
  return &jit->pages[idx];
}

/* Called when code is overwritten by data: Forget generated code, reset memory attributes */
extern void JITPage_ClobberCode(const JITState *jit,JITPage *page);

/* Clobber code by address */
extern void JITPage_ClobberCodeByAddr(const JITState *jit,void *addr);

#endif
