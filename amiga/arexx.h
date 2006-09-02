/* arexx.h */
#ifndef ARCEM_AREXX_H
#define ARCEM_AREXX_H

#include <proto/arexx.h>
#include <classes/arexx.h>

extern void ARexx_Init(void);
extern void ARexx_Handle(void);
extern void ARexx_Execute(char *);
BOOL arexx_quit;

#endif
