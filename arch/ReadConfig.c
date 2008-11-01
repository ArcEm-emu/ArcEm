/*
  Read the arc emulator configuration file.

  (c) David Alan Gilbert 1997
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../armdefs.h"
#include "../armopts.h"

#include "ArcemConfig.h"
#include "ReadConfig.h"
#include "armarc.h"
#include "hdc63463.h"

/* Returns 1 if it was all OK - 0 if it failed

  The config file is read from ~/.arcemrc
*/

int ReadConfigFile(ARMul_State *state)
{
    const char *envvar;
    const char *basename;
    char *env;
    char tmpbuf[1024];
    FILE *fConf;

/* FIXME:  This should use the SYSTEM_* preprocessor macros set up in
 * Makefile, not some platform-dependent one. */
#if defined(WIN32)
    envvar = NULL;
    basename = "arcemrc";
#elif defined(AMIGA)
    envvar = NULL;
    basename = ".arcemrc";
#elif defined(MACOSX)
    envvar = "HOME";
    basename = "arcem/.arcemrc";
#else
    envvar = "HOME";
    basename = ".arcemrc";
#endif

    if (envvar && (env = getenv(envvar)) == NULL) {
        fprintf(stderr, "configuration file is $%s/%s but $%s isn't "
            "set.", envvar, basename, envvar);
        return 0;
    }

    *tmpbuf = '\0';
    if (envvar) {
        strcat(tmpbuf, env);
        strcat(tmpbuf, "/");
    }
    strcat(tmpbuf, basename);

    if ((fConf = fopen(tmpbuf, "r")) == NULL) {
        fprintf(stderr, "couldn't open config file: %s\n", tmpbuf);
        return 0;
    }

  while (!feof(fConf)) {
    char *tptr;

    if (fgets(tmpbuf, 1023, fConf)==NULL) {
      fprintf(stderr, "Failed to read config file header line\n");
      return 0;
    }

    /* Remove CR,LF, spaces, etc from the end of line */
    for(tptr=(tmpbuf+strlen(tmpbuf));(tptr>=tmpbuf) && (*tptr<33);tptr--) {
      *tptr='\0';
    }

    if (strcmp(tmpbuf,"MFM disc") == 0) {
      /* The format of these lines is 
        drive number (0..3)
        Number of cylinders
        Number of heads
        Number of sectors
        Record length
      */

      unsigned int drivenum,numcyl,numheads,numsect,reclength;
      if (fscanf(fConf,"%u %u %u %u %u\n",&drivenum,&numcyl,&numheads,&numsect,&reclength)!=5) {
        fprintf(stderr,"Failed to read MFM disc data line\n");
        return 0;
      }
      
      if (drivenum>3) {
        fprintf(stderr,"Invalid drive number in MFM disc data line\n");
        return 0;
      }

      hArcemConfig.aST506DiskShapes[drivenum].NCyls        = numcyl;
      hArcemConfig.aST506DiskShapes[drivenum].NHeads       = numheads;
      hArcemConfig.aST506DiskShapes[drivenum].NSectors     = numsect;
      hArcemConfig.aST506DiskShapes[drivenum].RecordLength = reclength;
    } else {
      fprintf(stderr,"Unknown configuration file key '%s'\n",tmpbuf);
      return 0;
    }
  }

  return 1;

} /* ReadConfigFile */
