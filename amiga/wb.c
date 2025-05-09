/* Amiga Workbench launch routines.  Written by Chris Young    *
 *  Changes made to commandline parsing (arch/ArcemConfig.c)   *
 *  and config file parsing (arch/ReadConfig.c)                *
 *  need to be merged into this file to enable options to be   *
 *  set by tooltypes.                                          */

#include <workbench/startup.h>
#include <stdio.h>
#include <stdlib.h>

#include <proto/dos.h>
#include <proto/icon.h>

#include "ArcemConfig.h"
#include "platform.h"
#include "dbugsys.h"
#include "displaydev.h"

#ifdef __amigaos4__
#define CurrentDir SetCurrentDir
#endif

int force8bit;
int swapmousebuttons;
BOOL anymonitor;
BOOL use_ocm;

struct Library *IconBase;
#ifdef __amigaos4__
struct IconIFace *IIcon;
#endif

void wblaunch(struct WBStartup *, ArcemConfig *pConfig);

static void closewblibs(void)
{
	/* We only need these libraries to read tooltypes, so if they don't open
		we can just skip the tooltypes and carry on */

#ifdef __amigaos4__
	if(IIcon) DropInterface((struct Interface *)IIcon);
#endif
	CloseLibrary(IconBase);

/*
	if(IDOS)
	{
		IExec->DropInterface((struct Interface *)IDOS);
		IExec->CloseLibrary(DOSBase);
	}
*/
}


static void gettooltypes(struct WBArg *wbarg, ArcemConfig *pConfig)
{
	struct DiskObject *dobj;
	char **toolarray;
	char *s;

	force8bit = 0;
	swapmousebuttons = 0;
	anymonitor = FALSE;
	use_ocm = FALSE;

	if((*wbarg->wa_Name) && (dobj=GetDiskObject(wbarg->wa_Name)))
	{
		toolarray = dobj->do_ToolTypes;

		if((s = (char *)FindToolType(toolarray,"ROM")))
		{
        	char *sNewRomName = strdup(s);
        
        	/* Only replace the romname if we successfully allocated a new string */
        	if(sNewRomName)
			{
          		/* Free up the old one */
          		free(pConfig->sRomImageName);

		        pConfig->sRomImageName = sNewRomName;
			}
        }

#if defined(EXTNROM_SUPPORT)
		if((s = (char *)FindToolType(toolarray,"EXTNROMDIR")))
		{
        	char *sNewExtnRomDir = strdup(s);

	        if(sNewExtnRomDir)
			{
    	      /* Free up the old one */
        	  free(pConfig->sEXTNDirectory);

	          pConfig->sEXTNDirectory = sNewExtnRomDir;
			}
        }

#endif /* EXTNROM_SUPPORT */
#if defined(HOSTFS_SUPPORT)
		if((s = (char *)FindToolType(toolarray,"HOSTFSDIR")))
		{
	        char *sNewHostFSDir = strdup(s);
        
        	/* Only replace the directory if we successfully allocated a new string */
        	if(sNewHostFSDir)
			{
          		/* Free up the old one */
          		free(pConfig->sHostFSDirectory);

          		pConfig->sHostFSDirectory = sNewHostFSDir;
			}
        }
#endif /* HOSTFS_SUPPORT */

		if((s = (char *)FindToolType(toolarray,"MEMORY")))
		{
			if(MatchToolValue(s,"256K"))
          		pConfig->eMemSize = MemSize_256K;

			if(MatchToolValue(s,"512K"))
          		pConfig->eMemSize = MemSize_512K;

			if(MatchToolValue(s,"1M"))
          		pConfig->eMemSize = MemSize_1M;

			if(MatchToolValue(s,"2M"))
          		pConfig->eMemSize = MemSize_2M;

			if(MatchToolValue(s,"4M"))
          		pConfig->eMemSize = MemSize_4M;

			if(MatchToolValue(s,"8M"))
          		pConfig->eMemSize = MemSize_8M;

			if(MatchToolValue(s,"12M"))
          		pConfig->eMemSize = MemSize_12M;

			if(MatchToolValue(s,"16M"))
          		pConfig->eMemSize = MemSize_16M;
      }

		if((s = (char *)FindToolType(toolarray,"PROCESSOR")))
		{
			if(MatchToolValue(s,"ARM2"))
		         pConfig->eProcessor = Processor_ARM2;

			if(MatchToolValue(s,"ARM250"))
		         pConfig->eProcessor = Processor_ARM250;

			if(MatchToolValue(s,"ARM3"))
		         pConfig->eProcessor = Processor_ARM3;

		}

		if(FindToolType(toolarray,"FORCE8BIT")) force8bit=1;
		if(FindToolType(toolarray,"SWAPBUTTONS")) swapmousebuttons=1;
		if(FindToolType(toolarray,"ANYMONITOR")) anymonitor = TRUE;
		if(FindToolType(toolarray,"ENABLEOCM")) use_ocm = TRUE;

		if(FindToolType(toolarray, "USEUPDATEFLAGS"))
			DisplayDev_UseUpdateFlags = 1;

		if((s = (char *)FindToolType(toolarray, "FRAMESKIP")))
			DisplayDev_FrameSkip = atoi(s);
		else DisplayDev_FrameSkip = 0;

		if(FindToolType(toolarray, "AUTOUPDATEFLAGS"))
			DisplayDev_AutoUpdateFlags = 1;

		if(FindToolType(toolarray, "NOCONSOLEOUTPUT"))
		{
			/* if we are launching from WB, we don't need console windows popping up */
			fclose(stderr);
			stderr = fopen("NIL:","w");

			fclose(stdout);
			stdout = fopen("NIL:","w");
		}

		/* This code implements ReadConfig.c via tooltypes - it searches for
			ST506DISC, but it will only support 1 line atm.
			It is literally a copy'n'paste of the other code with fscanf(fConf)
			changed to sscanf(s) */

			if((s = (char *)FindToolType(toolarray,"ST506DISC")))
			{
	      		unsigned int drivenum,numcyl,numheads,numsect,reclength;
    			if (sscanf(s,"%u %u %u %u %u\n",&drivenum,&numcyl,&numheads,&numsect,&reclength)!=5)
				{
        			warn("Failed to read MFM disc data line\n");
        			return;
      			}
      
				if (drivenum>3)
				{
        			warn("Invalid drive number in MFM disc data line\n");
        			return;
      			}

			    pConfig->aST506DiskShapes[drivenum].NCyls        = numcyl;
      			pConfig->aST506DiskShapes[drivenum].NHeads       = numheads;
      			pConfig->aST506DiskShapes[drivenum].NSectors     = numsect;
      			pConfig->aST506DiskShapes[drivenum].RecordLength = reclength;
        }


		FreeDiskObject(dobj);
	}

}

void wblaunch(struct WBStartup *WBenchMsg, ArcemConfig *pConfig)
{
	struct WBArg *wbarg;
	long i;
	int olddir;

	#ifdef __amigaos4__
    IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;
	#endif
	
	if((IconBase = OpenLibrary("icon.library",37)))
	{
	#ifdef __amigaos4__
		IIcon = (struct IconIFace *)GetInterface(IconBase,"main",1,NULL);
	#endif
	}
	else
	{
		closewblibs();
		return;
	}

	if((DOSBase = OpenLibrary("dos.library",37)))
	{
	#ifdef __amigaos4__
		IDOS = (struct DOSIFace *)GetInterface(DOSBase,"main",1,NULL);
	#endif
	}
	else
	{
		closewblibs();
		return;
	}

	for(i=0,wbarg=WBenchMsg->sm_ArgList;i<WBenchMsg->sm_NumArgs;i++,wbarg++)
	{
		olddir =-1;
		if((wbarg->wa_Lock)&&(*wbarg->wa_Name))
			olddir = CurrentDir(wbarg->wa_Lock);

		gettooltypes(wbarg, pConfig);

		if(olddir !=-1) CurrentDir(olddir);
	}

	closewblibs();
}
