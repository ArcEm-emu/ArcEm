/*
  ArcemConfig

  (c) 2006 P Howkins

  Part of Arcem released under the GNU GPL, see file COPYING
  for details.

  ArcemConfig - Stores the running configuration of the Arcem
  emulator.
 
  See usage instructions in the ArcemConfig.h header file.
*/
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ArcemConfig.h"
#include "Version.h"
#include "ControlPane.h"

#ifdef AMIGA
#include <workbench/startup.h>
#endif

// Local functions
static char *arcemconfig_StringDuplicate(const char *sInput);



// Global Variable holding the configuration runtime options of
// the emulator
ArcemConfig hArcemConfig;

/** 
 * ArcemConfig_SetupDefaults
 *
 * Called on program startup, sets up the defaults for
 * all the configuration values for the emulator
 */
void ArcemConfig_SetupDefaults(void)
{
  // Default to 4MB of memory
  hArcemConfig.eMemSize = MemSize_4M;

  // We default to an ARM 2AS architecture (includes SWP) without a cache
  hArcemConfig.eProcessor = Processor_ARM250;

  hArcemConfig.sRomImageName = arcemconfig_StringDuplicate("ROM");
  // If we've run out of memory this early, something is very wrong
  if(NULL == hArcemConfig.sRomImageName) {
    ControlPane_Error(EXIT_FAILURE,"Failed to allocate memory for initial configuration. Please free up more memory.\n");
  }

#if defined(EXTNROM_SUPPORT)
  // The default directory is extnrom in the current working directory
  hArcemConfig.sEXTNDirectory = arcemconfig_StringDuplicate("extnrom");
  // If we've run out of memory this early, something is very wrong
  if(NULL == hArcemConfig.sEXTNDirectory) {
    ControlPane_Error(EXIT_FAILURE,"Failed to allocate memory for initial configuration. Please free up more memory.\n");
  }
#endif /* EXTNROM_SUPPORT */

#if defined(HOSTFS_SUPPORT)
  // The default directory is hostfs in the current working directory
#ifdef AMIGA
  hArcemConfig.sHostFSDirectory = arcemconfig_StringDuplicate("hostfs");
#else
  hArcemConfig.sHostFSDirectory = arcemconfig_StringDuplicate("./hostfs");
#endif
  // If we've run out of memory this early, something is very wrong
  if(NULL == hArcemConfig.sHostFSDirectory) {
    ControlPane_Error(EXIT_FAILURE,"Failed to allocate memory for initial configuration. Please free up more memory.\n");
  }

#endif /* HOSTFS_SUPPORT */

  /* Default for ST506 drive details is all NULL/zeros */
  memset(hArcemConfig.aST506DiskShapes, 0, sizeof(struct HDCshape) * 4);


#if defined(SYSTEM_riscos_single)
  hArcemConfig.eDisplayDriver = DisplayDriver_Palettised;
  hArcemConfig.bRedBlueSwap = false;
  hArcemConfig.bAspectRatioCorrection = true;
  hArcemConfig.bUpscale = true;
  hArcemConfig.bNoLowColour = false;
  hArcemConfig.iMinResX = 0;
  hArcemConfig.iMinResY = 0;
  hArcemConfig.iLCDResX = 0;
  hArcemConfig.iLCDResY = 0;
  hArcemConfig.iTweakMenuKey1 = 104; /* Left windows key */
  hArcemConfig.iTweakMenuKey2 = 105; /* Right windows key */
#endif
}

/**
 * ArcemConfig_ParseCommandLine
 *
 * Given the commandline arguments that the program was started with,
 * parse and fill in the runtime options into the global configuration
 * structure. Will exit the program if command line arguments are
 * incorrectly formatted (or the --help or --version argument is used).
 *
 * @param argc Number of entries in argv
 * @param argv Array of char*'s represented space seperated commandline arguments
 */
void ArcemConfig_ParseCommandLine(int argc, char *argv[])
{
  int iArgument = 0;
  char sHelpString[] =
    "Arcem <Options>\n"
    " Where options are one or more of the following\n"
    "  --help - Display this message and exit\n"
    "  --version - Display the version number and exit\n"
    "  --rom <value> - String of the location of the rom image\n"
#if defined(EXTNROM_SUPPORT)
    "  --extnromdir <value> - String of the location of the extension rom directory\n"
#endif /* EXTNROM_SUPPORT */
#if defined(HOSTFS_SUPPORT)
    "  --hostfsdir <value> - String of the location of the hostfs directory\n"
#endif /* HOSTFS_SUPPORT */
    "  --memory <value> - Set the memory size of the emulator\n"
    "     Where value is one of '256K', '512K', '1M', '2M', '4M',\n"
    "     '8M', '12M' or '16M'\n"
    "  --processor <value> - Set the emulated CPU\n"
    "     Where value is one of 'ARM2', 'ARM250', 'ARM3'\n"
#if defined(SYSTEM_riscos_single)
    "  --display <mode> - Select display driver, 'pal' or 'std'\n"
    "  --rbswap - Swap red & blue in 16bpp mode (e.g. for Iyonix with GeForce FX)\n"
    "  --noaspect - Disable aspect ratio correction\n"
    "  --noupscale - Disable upscaling\n"
    "  --nolowcolour - Disable 1/2/4bpp modes (e.g. for Iyonix with Aemulor running)\n"
    "  --minres <x> <y> - Specify dimensions of smallest screen mode to use\n"
    "  --lcdres <x> <y> - Specify native resolution of your monitor (to avoid using\n"
    "     modes that won't scale well on an LCD)\n"
    "  --menukeys <a> <b> - Specify which key numbers open the tweak menu\n"
#endif /* SYSTEM_riscos_single */
    ;

  // No commandline arguments?
  if(0 == argc) {
    // There should always be at least 1, the program name
#ifdef AMIGA
// Unless we are launching from Workbench...
	wblaunch((struct WBStartup *)argv);
#endif
    return;
  }

  if(1 == argc) {
    // No arguments other than the program name, no work to do
    return;
  }

  // We don't care about argument 0 as that's the program name
  iArgument = 1;

  while(iArgument < argc) {
    if(0 == strcmp("--version", argv[iArgument])) {
      printf("Arcem %s\n", Version);
      
      exit(EXIT_SUCCESS);
    }
    else if(0 == strcmp("--help", argv[iArgument])) {
      printf("%s", sHelpString);
      
      exit(EXIT_SUCCESS);
    }
    else if(0 == strcmp("--rom", argv[iArgument])) {
      if(iArgument+1 < argc) { // Is there a following argument?
        char *sNewRomName = arcemconfig_StringDuplicate(argv[iArgument + 1]);
        
        // Only replace the romname if we successfully allocated a new string
        if(sNewRomName) {
          // Free up the old one
          free(hArcemConfig.sRomImageName);

          hArcemConfig.sRomImageName = sNewRomName;
        }

        iArgument += 2;
      } else {
        // No argument following the --rom option
        ControlPane_Error(EXIT_FAILURE,"No argument following the --rom option\n");
      }
    }
#if defined(EXTNROM_SUPPORT)
    else if(0 == strcmp("--extnromdir", argv[iArgument])) {
      if(iArgument+1 < argc) { // Is there a following argument?
        char *sNewExtnRomDir = arcemconfig_StringDuplicate(argv[iArgument + 1]);
        
        // Only replace the directory if we successfully allocated a new string
        if(sNewExtnRomDir) {
          // Free up the old one
          free(hArcemConfig.sEXTNDirectory);

          hArcemConfig.sEXTNDirectory = sNewExtnRomDir;
        }

        iArgument += 2;
      } else {
        // No argument following the --extnromdir option
        ControlPane_Error(EXIT_FAILURE,"No argument following the --extnromdir option\n");
      }
    }
#endif /* EXTNROM_SUPPORT */
#if defined(HOSTFS_SUPPORT)
    else if(0 == strcmp("--hostfsdir", argv[iArgument])) {
      if(iArgument+1 < argc) { // Is there a following argument?
        char *sNewHostFSDir = arcemconfig_StringDuplicate(argv[iArgument + 1]);
        
        // Only replace the directory if we successfully allocated a new string
        if(sNewHostFSDir) {
          // Free up the old one
          free(hArcemConfig.sHostFSDirectory);

          hArcemConfig.sHostFSDirectory = sNewHostFSDir;
        }

        iArgument += 2;
      } else {
        // No argument following the --hostfsdir option
        ControlPane_Error(EXIT_FAILURE,"No argument following the --hostfsdir option\n");
      }
    }
#endif /* HOSTFS_SUPPORT */
    else if(0 == strcmp("--memory", argv[iArgument])) {
      if(iArgument+1 < argc) { // Is there a following argument?
        if(0 == strcmp("256K", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_256K;

          iArgument += 2;
        }
        else if(0 == strcmp("512K", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_512K;

          iArgument += 2;
        }
        else if(0 == strcmp("1M", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_1M;

          iArgument += 2;
        }
        else if(0 == strcmp("2M", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_2M;

          iArgument += 2;
        }
        else if(0 == strcmp("4M", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_4M;

          iArgument += 2;
        }
        else if(0 == strcmp("8M", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_8M;

          iArgument += 2;
        }
        else if(0 == strcmp("12M", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_12M;

          iArgument += 2;
        }
        else if(0 == strcmp("16M", argv[iArgument + 1])) {
          hArcemConfig.eMemSize = MemSize_16M;

          iArgument += 2;
        }
        else {
          ControlPane_Error(EXIT_FAILURE,"Unrecognised value to the --memory option\n");
        }

      } else {
        // No argument following the --memory option
        ControlPane_Error(EXIT_FAILURE,"No argument following the --memory option\n");
      }
    }
    else if(0 == strcmp("--processor", argv[iArgument])) {
      if(iArgument+1 < argc) { // Is there a following argument?
        if(0 == strcmp("ARM2", argv[iArgument + 1])) {
          hArcemConfig.eProcessor = Processor_ARM2;

          iArgument += 2;
        }
        else if(0 == strcmp("ARM250", argv[iArgument + 1])) {
          hArcemConfig.eProcessor = Processor_ARM250;

          iArgument += 2;
        }
        else if(0 == strcmp("ARM3", argv[iArgument + 1])) {
          hArcemConfig.eProcessor = Processor_ARM3;

          iArgument += 2;
        }
        else {
          ControlPane_Error(EXIT_FAILURE,"Unrecognised value to the --processor option\n");
        }
      } else {
        // No argument following the --processor option
        ControlPane_Error(EXIT_FAILURE,"No argument following the --processor option\n");
      }
    }
#if defined(SYSTEM_riscos_single)
    else if(0 == strcmp("--display", argv[iArgument])) {
      if(iArgument+1 < argc) { // Is there a following argument?
        if(0 == strcmp("pal", argv[iArgument + 1])) {
          hArcemConfig.eDisplayDriver = DisplayDriver_Palettised;
          iArgument += 2;
        }
        else if(0 == strcmp("std", argv[iArgument + 1])) {
          hArcemConfig.eDisplayDriver = DisplayDriver_Standard;
          iArgument += 2;
        }
        else {
          ControlPane_Error(EXIT_FAILURE,"Unrecognised value to the --display option\n");
        }
      } else {
        ControlPane_Error(EXIT_FAILURE,"No argument following the --display option\n");
      }
    } else if(0 == strcmp("--rbswap",argv[iArgument])) {
      hArcemConfig.bRedBlueSwap = true;
      iArgument += 1;
    } else if(0 == strcmp("--noaspect",argv[iArgument])) {
      hArcemConfig.bAspectRatioCorrection = false;
      iArgument += 1;
    } else if(0 == strcmp("--noupscale",argv[iArgument])) {
      hArcemConfig.bUpscale = false;
      iArgument += 1;
    } else if(0 == strcmp("--nolowcolour",argv[iArgument])) {
      hArcemConfig.bNoLowColour = true;
      iArgument += 1;
    } else if(0 == strcmp("--minres",argv[iArgument])) {
      if(iArgument+2 < argc) {
        hArcemConfig.iMinResX = atoi(argv[iArgument+1]);
        hArcemConfig.iMinResY = atoi(argv[iArgument+2]);
        iArgument += 3;
      } else {
        ControlPane_Error(EXIT_FAILURE,"Not enough arguments following the --minres option\n");
      }
    } else if(0 == strcmp("--lcdres",argv[iArgument])) {
      if(iArgument+2 < argc) {
        hArcemConfig.iLCDResX = atoi(argv[iArgument+1]);
        hArcemConfig.iLCDResY = atoi(argv[iArgument+2]);
        iArgument += 3;
      } else {
        ControlPane_Error(EXIT_FAILURE,"Not enough arguments following the --lcdres option\n");
      }
    } else if(0 == strcmp("--menukeys",argv[iArgument])) {
      if(iArgument+2 < argc) {
        hArcemConfig.iTweakMenuKey1 = atoi(argv[iArgument+1]);
        hArcemConfig.iTweakMenuKey2 = atoi(argv[iArgument+2]);
        iArgument += 3;
      } else {
        ControlPane_Error(EXIT_FAILURE,"Not enough arguments following the --menukeys option\n");
      }
    }
#endif /* SYSTEM_riscos_single */
    else {
      ControlPane_Error(EXIT_FAILURE,"Unrecognised option '%s', try --help\n", argv[iArgument]);
    }
  }
}

/**
 * arcemconfig_StringDuplicate
 *
 * Equivalent of the POSIX function strdup. All because it
 * isn't in ANSI/ISO C.
 *
 * @param sInput String to copy
 * @returns Copy of string or NULL on error (memory failure)
 */
static char *arcemconfig_StringDuplicate(const char *sInput)
{
  char *sNew;
  assert(sInput);

  sNew = calloc(strlen(sInput) + 1, 1); /* +1 is the string null terminator */
  if(sNew) {
    strcpy(sNew, sInput);
  }

  return sNew;
}
