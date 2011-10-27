#ifndef prof_h
#define prof_h

#ifdef PROFILE_ENABLED

extern void Prof_Init(void);
extern void Prof_Dump(void);
extern void Prof_BeginFunc(const void *);
extern void Prof_EndFunc(const void *);
extern void Prof_Begin(const char *);
extern void Prof_End(const char *);

#else

#define Prof_Init() ((void) 0)
#define Prof_Dump() ((void) 0)
#define Prof_BeginFunc(x) ((void) 0)
#define Prof_EndFunc(x) ((void) 0)
#define Prof_Begin(x) ((void) 0)
#define Prof_End(x) ((void) 0)

#endif

#endif