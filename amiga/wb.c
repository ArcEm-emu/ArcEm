/* Amiga Workbench launch routines.  Written by Chris Young    *
 *  Changes made to commandline parsing (arch/ArcemConfig.c)   *
 *  and config file parsing (arch/ReadConfig.c)                *
 *  need to be merged into this file to enable options to be   *
 *  set by tooltypes.                                          */

#include <workbench/startup.h>
#include <stdio.h>

#include "ArcemConfig.h"
#include "platform.h"
#include "displaydev.h"

void wblaunch(struct WBStartup *);
void closewblibs(void);
void gettooltypes(struct WBArg *);

void closewblibs(void)
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


void gettooltypes(struct WBArg *wbarg)
{
	struct DiskObject *dobj;
	CONST_STRPTR *toolarray;
	char *s;

	force8bit = 0;
	swapmousebuttons = 0;
	anymonitor = FALSE;
	use_ocm = FALSE;

	if((*wbarg->wa_Name) && (dobj=GetDiskObject(wbarg->wa_Name)))
	{
		toolarray = (CONST_STRPTR *)dobj->do_ToolTypes;

		if(s = (char *)FindToolType(toolarray,"ROM"))
		{
        	char *sNewRomName = strdup(s);
        
        	// Only replace the romname if we successfully allocated a new string
        	if(sNewRomName)
			{
          		// Free up the old one
          		free(hArcemConfig.sRomImageName);

		        hArcemConfig.sRomImageName = sNewRomName;
			}
        }

#if defined(EXTNROM_SUPPORT)
		if(s = (char *)FindToolType(toolarray,"EXTNROMDIR"))
		{
        	char *sNewExtnRomDir = strdup(s);

	        if(sNewExtnRomDir)
			{
    	      // Free up the old one
        	  free(hArcemConfig.sEXTNDirectory);

	          hArcemConfig.sEXTNDirectory = sNewExtnRomDir;
			}
        }

#endif /* EXTNROM_SUPPORT */
#if defined(HOSTFS_SUPPORT)
		if(s = (char *)FindToolType(toolarray,"HOSTFSDIR"))
		{
	        char *sNewHostFSDir = strdup(s);
        
        	// Only replace the directory if we successfully allocated a new string
        	if(sNewHostFSDir)
			{
          		// Free up the old one
          		free(hArcemConfig.sHostFSDirectory);

          		hArcemConfig.sHostFSDirectory = sNewHostFSDir;
			}
        }
#endif /* HOSTFS_SUPPORT */

		if(s = (char *)FindToolType(toolarray,"MEMORY"))
		{
			if(MatchToolValue(s,"256K"))
          		hArcemConfig.eMemSize = MemSize_256K;

			if(MatchToolValue(s,"512K"))
          		hArcemConfig.eMemSize = MemSize_512K;

			if(MatchToolValue(s,"1M"))
          		hArcemConfig.eMemSize = MemSize_1M;

			if(MatchToolValue(s,"2M"))
          		hArcemConfig.eMemSize = MemSize_2M;

			if(MatchToolValue(s,"4M"))
          		hArcemConfig.eMemSize = MemSize_4M;

			if(MatchToolValue(s,"8M"))
          		hArcemConfig.eMemSize = MemSize_8M;

			if(MatchToolValue(s,"12M"))
          		hArcemConfig.eMemSize = MemSize_12M;

			if(MatchToolValue(s,"16M"))
          		hArcemConfig.eMemSize = MemSize_16M;
      }

		if(s = (char *)FindToolType(toolarray,"PROCESSOR"))
		{
			if(MatchToolValue(s,"ARM2"))
		         hArcemConfig.eProcessor = Processor_ARM2;

			if(MatchToolValue(s,"ARM250"))
		         hArcemConfig.eProcessor = Processor_ARM250;

			if(MatchToolValue(s,"ARM3"))
		         hArcemConfig.eProcessor = Processor_ARM3;

		}

		if(FindToolType(toolarray,"FORCE8BIT")) force8bit=1;
		if(FindToolType(toolarray,"SWAPBUTTONS")) swapmousebuttons=1;
		if(FindToolType(toolarray,"ANYMONITOR")) anymonitor = TRUE;
		if(FindToolType(toolarray,"ENABLEOCM")) use_ocm = TRUE;

		if(FindToolType(toolarray, "USEUPDATEFLAGS"))
			DisplayDev_UseUpdateFlags = 1;

		if(s = (char *)FindToolType(toolarray, "FRAMESKIP"))
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

			if(s = (char *)FindToolType(toolarray,"ST506DISC"))
			{
	      		unsigned int drivenum,numcyl,numheads,numsect,reclength;
    			if (sscanf(s,"%u %u %u %u %u\n",&drivenum,&numcyl,&numheads,&numsect,&reclength)!=5)
				{
        			fprintf(stderr,"Failed to read MFM disc data line\n");
        			return;
      			}
      
				if (drivenum>3)
				{
        			fprintf(stderr,"Invalid drive number in MFM disc data line\n");
        			return;
      			}

			    hArcemConfig.aST506DiskShapes[drivenum].NCyls        = numcyl;
      			hArcemConfig.aST506DiskShapes[drivenum].NHeads       = numheads;
      			hArcemConfig.aST506DiskShapes[drivenum].NSectors     = numsect;
      			hArcemConfig.aST506DiskShapes[drivenum].RecordLength = reclength;
        }


		FreeDiskObject(dobj);
	}

}

void wblaunch(struct WBStartup *WBenchMsg)
{
	struct WBArg *wbarg;
	long i;
	int olddir;

	#ifdef __amigaos4__
    IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;
	#endif
	
	if(IconBase = OpenLibrary("icon.library",37))
	{
	#ifdef __amigaos4__
		IIcon = GetInterface(IconBase,"main",1,NULL);
	#endif
	}
	else
	{
		closewblibs();
		return;
	}

	if(DOSBase = OpenLibrary("dos.library",37))
	{
	#ifdef __amigaos4__
		IDOS = GetInterface(DOSBase,"main",1,NULL);
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

		gettooltypes(wbarg);

		if(olddir !=-1) CurrentDir(olddir);
	}

	closewblibs();
}
