/*
  Read the arc emulator configuration file.

  (c) David Alan Gilbert 1997
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../armdefs.h"
#include "../armopts.h"

#include "ReadConfig.h"
#include "armarc.h"
#include "hdc63463.h"

/* Returns 1 if it was all OK - 0 if it failed

  The config file is read from ~/.arcemrc
*/
int ReadConfigFile(ARMul_State *state) {
  FILE *fConf;
  char *HomeVar = getenv("HOME");
  char *nameConf;
  char tmpbuf[1024];

  if (HomeVar==NULL) {
    fprintf(stderr,"Couldn't read $HOME and thus couldn't load config file\n");
    return 0;
  };

  if (nameConf = malloc(strlen(HomeVar) + 32),nameConf==NULL) {
    fprintf(stderr,"Couldn't allocate memory for name of config file\n");
    return 0;
  };

  sprintf(nameConf,"%s/.arcemrc",HomeVar);
  if (fConf=fopen(nameConf,"r"),fConf==NULL) {
    fprintf(stderr,"Couldn't open config file\n");
    return 0;
  };
  
  while (!feof(fConf)) {
    char *tptr;

    if (fgets(tmpbuf, 1023, fConf)==NULL) {
      fprintf(stderr, "Failed to read config file header line\n");
      return 0;
    };

    /* Remove CR,LF, spaces, etc from the end of line */
    for(tptr=(tmpbuf+strlen(tmpbuf));(tptr>=tmpbuf) && (*tptr<33);tptr--)
      *tptr='\0';

    if (stricmp(tmpbuf,"MFM disc") == 0) {
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
      };
      
      if (drivenum>3) {
        fprintf(stderr,"Invalid drive number in MFM disc data line\n");
        return 0;
      };
      
      HDC.configshape[drivenum].NCyls        = numcyl;
      HDC.configshape[drivenum].NHeads       = numheads;
      HDC.configshape[drivenum].NSectors     = numsect;
      HDC.configshape[drivenum].RecordLength = reclength;
    } else {
      fprintf(stderr,"Unknown configuration file key '%s'\n",tmpbuf);
      return 0;
    }
  };

  return 1;

}; /* ReadConfigFile */
