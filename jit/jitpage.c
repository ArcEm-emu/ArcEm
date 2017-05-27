#include <stdlib.h>
#include "jitpage.h"
#include "jit.h"
#include "codeblocks.h"
#include "memattr.h"

void JITPage_Init(JITState *jit,uint32_t romramchunksize)
{
  int numpages = romramchunksize/JITPAGE_SIZE;
  jit->pages = (JITPage *) calloc(numpages,sizeof(JITPage));
  while (numpages--)
  {
    jit->pages[numpages].codeblock = -1;
  }
}

void JITPage_ForgetCode(const JITState *jit,JITPage *page)
{
  if (page->codeblock == -1)
  {
    return;
  }
  /* Unlink with the generated code */
  CodeBlock_InvalidateID(page->codeblock);
  page->codeblock = -1;
  /* Reset all the pointers */
  void *addr = JITPage_StartOfPage(jit,page);
  JITFunc *func = JIT_Phy2Func(jit, addr);
  int i;
  for(i=0;i<JITPAGE_SIZE;i+=4)
  {
    *func++ = &JIT_Generate;
  }
}

void JITPage_ClobberCode(const JITState *jit,JITPage *page)
{
  /* Release code */
  JITPage_ForgetCode(jit,page);
  /* Reset memory attributes */
  void *phy = JITPage_StartOfPage(jit,page);
  MemAttr_ClearFlagsRange(jit, phy, ((uint8_t *) phy) + JITPAGE_SIZE);
}

void JITPage_ClobberCodeByAddr(const JITState *jit,void *addr)
{
  JITPage_ClobberCode(jit,JITPage_Get(jit,addr));
}
