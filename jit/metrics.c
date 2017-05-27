#ifdef DEBUG_JIT_METRICS

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "metrics.h"

JITMetrics jitmetrics;

static const char *terminate_reasons[] = {
"TerminateReason_NOP",
"TerminateReason_Branch",
"TerminateReason_DataProc",
"TerminateReason_Multiply",
"TerminateReason_LDRSTR",
"TerminateReason_LDMSTM",
"TerminateReason_SWI",
"TerminateReason_OtherInstr",
"TerminateReason_Normal",
"TerminateReason_Special",
};

void JITMetrics_Dump(void)
{
  uint64_t execute_total = 0;
  int i;
  fprintf(stderr,"%12u generate_count\n",jitmetrics.generate_count);
  fprintf(stderr,"%12u instructions_in\n",jitmetrics.instructions_in);
  fprintf(stderr,"%12u instructions_out\n",jitmetrics.instructions_out);
#ifdef DEBUG_JIT_METRICS_EXEC
  for(i=0;i<JITPAGE_SIZE/4;i++)
  {
    if (jitmetrics.execute_histogram[i])
    {
      fprintf(stderr,"%12u %4d execute_histogram\n",jitmetrics.execute_histogram[i],i+1);
      execute_total += ((uint64_t) jitmetrics.execute_histogram[i])*(i+1);
    }
  }
  fprintf(stderr,"%12" PRIu64 " execute_total\n",execute_total);
#endif
  fprintf(stderr,"%12u interpret_count\n",jitmetrics.interpret_count);
  for(i=0;i<TerminateReason_Count;i++)
  {
    fprintf(stderr,"%12u %s\n",jitmetrics.terminate_reason[i],terminate_reasons[i]);
  }
  for(i=0;i<JITPAGE_SIZE/4;i++)
  {
    if (jitmetrics.entry_points[i])
    {
      fprintf(stderr,"%12u %4d entry_points\n",jitmetrics.entry_points[i],i+1);
    }
  }
  for(i=0;i<JITPAGE_SIZE/4;i++)
  {
    if (jitmetrics.exit_points[i])
    {
      fprintf(stderr,"%12u %4d exit_points\n",jitmetrics.exit_points[i],i+1);
    }
  }
  memset(&jitmetrics,0,sizeof(jitmetrics));
}

#endif
