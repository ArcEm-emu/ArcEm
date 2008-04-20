/* Amiga Workbench launch routines.  Written by Chris Young    *
 *  Changes made to commandline parsing (arch/ArcemConfig.c)   *
 *  and config file parsing (arch/ReadConfig.c)                *
 *  need to be merged into this file to enable options to be   *
 *  set by tooltypes.                                          */

#include <workbench/startup.h>

#include <stdio.h>

#include "ArcemConfig.h"
#include "platform.h"

int force8bit=0;

void wblaunch(struct WBStartup *);
void closewblibs(void);
void gettooltypes(struct WBArg *);

void closewblibs(void)
{
	/* We only need these libraries to read tooltypes, so if they don't open
		we can just skip the tooltypes and carry on */

	if(IIcon)
	{
		IExec->DropInterface((struct Interface *)IIcon);
		IExec->CloseLibrary(IconBase);
	}

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

	if((*wbarg->wa_Name) && (dobj=IIcon->GetIconTags(wbarg->wa_Name,NULL)))
	{
		toolarray = (CONST_STRPTR *)dobj->do_ToolTypes;

		if(s = (char *)IIcon->FindToolType(toolarray,"ROM"))
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
		if(s = (char *)IIcon->FindToolType(toolarray,"EXTNROMDIR"))
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
		if(s = (char *)IIcon->FindToolType(toolarray,"HOSTFSDIR"))
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

		if(s = (char *)IIcon->FindToolType(toolarray,"MEMORY"))
		{
			if(IIcon->MatchToolValue(s,"256K"))
          		hArcemConfig.eMemSize = MemSize_256K;

			if(IIcon->MatchToolValue(s,"512K"))
          		hArcemConfig.eMemSize = MemSize_512K;

			if(IIcon->MatchToolValue(s,"1M"))
          		hArcemConfig.eMemSize = MemSize_1M;

			if(IIcon->MatchToolValue(s,"2M"))
          		hArcemConfig.eMemSize = MemSize_2M;

			if(IIcon->MatchToolValue(s,"4M"))
          		hArcemConfig.eMemSize = MemSize_4M;

			if(IIcon->MatchToolValue(s,"8M"))
          		hArcemConfig.eMemSize = MemSize_8M;

			if(IIcon->MatchToolValue(s,"12M"))
          		hArcemConfig.eMemSize = MemSize_12M;

			if(IIcon->MatchToolValue(s,"16M"))
          		hArcemConfig.eMemSize = MemSize_16M;

      }

		if(s = (char *)IIcon->FindToolType(toolarray,"PROCESSOR"))
		{
			if(IIcon->MatchToolValue(s,"ARM2"))
		         hArcemConfig.eProcessor = Processor_ARM2;

			if(IIcon->MatchToolValue(s,"ARM250"))
		         hArcemConfig.eProcessor = Processor_ARM250;

			if(IIcon->MatchToolValue(s,"ARM3"))
		         hArcemConfig.eProcessor = Processor_ARM3;

		}

		if(IIcon->FindToolType(toolarray,"FORCE8BIT")) force8bit=1;

		/* This code implements ReadConfig.c via tooltypes - it searches for
			ST506DISC, but it will only support 1 line atm.
			It is literally a copy'n'paste of the other code with fscanf(fConf)
			changed to sscanf(s) */

			if(s = (char *)IIcon->FindToolType(toolarray,"ST506DISC"))
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


		IIcon->FreeDiskObject(dobj);
	}

}

void wblaunch(struct WBStartup *WBenchMsg)
{
	struct WBArg *wbarg;
	long i;
	int olddir;

    IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;

	if(IconBase = IExec->OpenLibrary("icon.library",51))
	{
		IIcon = IExec->GetInterface(IconBase,"main",1,NULL);
	}
	else
	{
		closewblibs();
		return;
	}

	if(DOSBase = IExec->OpenLibrary("dos.library",51))
	{
		IDOS = IExec->GetInterface(DOSBase,"main",1,NULL);
	}
	else
	{
		closewblibs();
		return;
	}

	/* if we are launching from WB, we don't need console windows popping up */
		fclose(stderr);
		stderr = fopen("NIL:","w");

		fclose(stdout);
		stdout = fopen("NIL:","w");

	for(i=0,wbarg=WBenchMsg->sm_ArgList;i<WBenchMsg->sm_NumArgs;i++,wbarg++)
	{
		olddir =-1;
		if((wbarg->wa_Lock)&&(*wbarg->wa_Name))
			olddir = IDOS->CurrentDir(wbarg->wa_Lock);

		gettooltypes(wbarg);

		if(olddir !=-1) IDOS->CurrentDir(olddir);
	}

	closewblibs();
}
