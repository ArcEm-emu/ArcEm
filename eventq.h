#ifndef EVENTQ_H
#define EVENTQ_H

#include <string.h>
#include "armdefs.h"

/* Event queue functions */

/* Initialise the queue */
extern void EventQ_Init(ARMul_State *state);

/* Remove an entry with a certain index */
static inline void EventQ_Remove(ARMul_State *state,int idx)
{
	if(--state->NumEvents)
	{
		memmove(&state->EventQ[idx],&state->EventQ[idx+1],state->NumEvents-idx);
	}
	else
	{
		EventQ_Init(state); /* Unlikely case */
	}
}

/* Reschedule an arbitrary entry, returns new index */
static inline int EventQ_Reschedule(ARMul_State *state,CycleCount eventtime,EventQ_Func func,int idx)
{
	while((idx > 0) && (((CycleDiff) (state->EventQ[idx-1].Time-eventtime)) > 0))
	{
		state->EventQ[idx] = state->EventQ[idx-1];
		idx--;
	}
	int top = state->NumEvents-1;
	while((idx < top) && (((CycleDiff) (state->EventQ[idx+1].Time-eventtime)) < 0))
	{
		state->EventQ[idx] = state->EventQ[idx+1];
		idx++;
	}
	state->EventQ[idx].Time = eventtime;
	state->EventQ[idx].Func = func;
	return idx;
}

/* Reschedule the head entry, returns new index */
static inline int EventQ_RescheduleHead(ARMul_State *state,CycleCount eventtime,EventQ_Func func)
{
	return EventQ_Reschedule(state,eventtime,func,0);
}

/* Insert new entry, returns index */
static inline int EventQ_Insert(ARMul_State *state,CycleCount eventtime,EventQ_Func func)
{
	/* Assume new events will occur far in the future, so insert from back */
	int idx = state->NumEvents++;
	while((idx > 0) && (((CycleDiff) (state->EventQ[idx-1].Time-eventtime)) > 0))
	{
		state->EventQ[idx] = state->EventQ[idx-1];
		idx--;
	}
	state->EventQ[idx].Time = eventtime;
	state->EventQ[idx].Func = func;
	return idx;
}

/* Return index of given event func, or -1 if not found */
static inline int EventQ_Find(ARMul_State *state,EventQ_Func func)
{
	int idx = state->NumEvents;
	while(--idx >= 0)
	{
		if(state->EventQ[idx].Func == func)
			break;
	}
	return idx;
}

#endif
