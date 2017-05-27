#ifndef JITMETRICS_HEADER
#define JITMETRICS_HEADER

#ifdef DEBUG_JIT_METRICS
#include "jitpage.h"
#include "decoder.h"

typedef enum {
  /* 0 ... InstrType_Count represent specific instructions that caused termination */
  TerminateReason_Normal = InstrType_Count, /* Termination due to JITResult_Normal */
  TerminateReason_Special, /* Execution has entered special memory region (or prefetch abort) */

  TerminateReason_Count,
} TerminateReason;

typedef struct {
  /* generate_count gives the number of blocks that have been generated */
  uint32_t generate_count;
  /* instructions_in counts the number of instructions that have been JITed */
  uint32_t instructions_in;
  /* instructions_out counts the number of instructions output by the JIT */
  uint32_t instructions_out;
  /* interpret_count counts the number of instructions that have been interpreted */
  uint32_t interpret_count;
  /* terminate_reason[N] gives the number of times that a code block has exited due to reason N */
  uint32_t terminate_reason[TerminateReason_Count];
#ifdef DEBUG_JIT_METRICS_EXEC
  /* execute_histogram[N] gives the number of times that a block of length N-1 instructions has been executed */
  uint32_t execute_histogram[JITPAGE_SIZE/4];
#endif
  /* entry_points[N] gives the number of times that a page has been generated with N-1 entry points */
  uint32_t entry_points[JITPAGE_SIZE/4];
  /* exit_points[N] gives the number of times that a page has been generated with N-1 exit points */
  uint32_t exit_points[JITPAGE_SIZE/4];
} JITMetrics;

extern JITMetrics jitmetrics;

extern void JITMetrics_Dump(void);

#endif /* DEBUG_JIT_METRICS */

#endif
