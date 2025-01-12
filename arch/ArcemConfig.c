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
#include "dbugsys.h"
#include "filecalls.h"

#include "libs/inih/ini.h"

#ifdef AMIGA
#include <workbench/startup.h>

void wblaunch(struct WBStartup *, ArcemConfig *pConfig);
#endif

/* Local functions */
static char *arcemconfig_StringDuplicate(const char *sInput);
static void arcemconfig_StringReplace(char** sPtr, const char* sNew);
static bool arcemconfig_StringToEnum(unsigned int* uPtr, const char* sInput, const ArcemConfig_Label *labels);

static const ArcemConfig_Label memsize_labels[] = {
    { "256K", MemSize_256K },
    { "512K", MemSize_512K },
    { "1M",   MemSize_1M   },
    { "2M",   MemSize_2M   },
    { "4M",   MemSize_4M   },
    { "8M",   MemSize_8M   },
    { "12M",  MemSize_12M  },
    { "16M",  MemSize_16M  },
    { NULL, 0 }
};

static const ArcemConfig_Label processor_labels[] = {
    { "ARM2",   Processor_ARM2 },
    { "ARM250", Processor_ARM250 },
    { "ARM3",   Processor_ARM3 },
    { NULL, 0 }
};

/** 
 * ArcemConfig_SetupDefaults
 *
 * Called on program startup, sets up the defaults for
 * all the configuration values for the emulator
 */
void ArcemConfig_SetupDefaults(ArcemConfig *pConfig)
{
  /* Default to 4MB of memory */
  pConfig->eMemSize = MemSize_4M;

  /* We default to an ARM 2AS architecture (includes SWP) without a cache */
  pConfig->eProcessor = Processor_ARM250;

  pConfig->sRomImageName = arcemconfig_StringDuplicate("ROM");
  /* If we've run out of memory this early, something is very wrong */
  if(NULL == pConfig->sRomImageName) {
    ControlPane_Error(EXIT_FAILURE,"Failed to allocate memory for initial configuration. Please free up more memory.\n");
  }

  pConfig->sCMOSFileName = NULL;

#if defined(EXTNROM_SUPPORT)
  /* The default directory is extnrom in the current working directory */
  pConfig->sEXTNDirectory = arcemconfig_StringDuplicate("extnrom");
  /* If we've run out of memory this early, something is very wrong */
  if(NULL == pConfig->sEXTNDirectory) {
    ControlPane_Error(EXIT_FAILURE,"Failed to allocate memory for initial configuration. Please free up more memory.\n");
  }
#endif /* EXTNROM_SUPPORT */

#if defined(HOSTFS_SUPPORT)
  /* The default directory is hostfs in the current working directory */
#ifdef AMIGA
  pConfig->sHostFSDirectory = arcemconfig_StringDuplicate("hostfs");
#else
  pConfig->sHostFSDirectory = arcemconfig_StringDuplicate("./hostfs");
#endif
  /* If we've run out of memory this early, something is very wrong */
  if(NULL == pConfig->sHostFSDirectory) {
    ControlPane_Error(EXIT_FAILURE,"Failed to allocate memory for initial configuration. Please free up more memory.\n");
  }

#endif /* HOSTFS_SUPPORT */

  /* Default for drive details is all NULL/zeros */
  memset(pConfig->aFloppyPaths, 0, sizeof(char *) * 4);
  memset(pConfig->aST506Paths, 0, sizeof(char *) * 4);
  memset(pConfig->aST506DiskShapes, 0, sizeof(struct HDCshape) * 4);

#if defined(SYSTEM_win)
  pConfig->eDisplayDriver = DisplayDriver_Standard;
  pConfig->bAspectRatioCorrection = true;
  pConfig->bUpscale = true;
#endif
#if defined(SYSTEM_riscos_single)
  pConfig->eDisplayDriver = DisplayDriver_Palettised;
  pConfig->bAspectRatioCorrection = true;
  pConfig->bUpscale = true;
  pConfig->bRedBlueSwap = false;
  pConfig->bNoLowColour = false;
  pConfig->iMinResX = 0;
  pConfig->iMinResY = 0;
  pConfig->iLCDResX = 0;
  pConfig->iLCDResY = 0;
  pConfig->iTweakMenuKey1 = 104; /* Left windows key */
  pConfig->iTweakMenuKey2 = 105; /* Right windows key */
#endif
}

static int ArcemConfig_Handler(void* user, const char* section,
    const char* name, const char* value)
{
    ArcemConfig *pConfig = (ArcemConfig *)user;
    unsigned int uValue;

    if (0 == strcmp(section, "machine")) {
        if (0 == strcmp(name, "rom")) {
            arcemconfig_StringReplace(&pConfig->sRomImageName, value);
        } else if (0 == strcmp(name, "hexcmos")) {
            arcemconfig_StringReplace(&pConfig->sCMOSFileName, value);
#if defined(EXTNROM_SUPPORT)
        } else if (0 == strcmp(name, "extnromdir")) {
            arcemconfig_StringReplace(&pConfig->sEXTNDirectory, value);
#endif
#if defined(HOSTFS_SUPPORT)
        } else if (0 == strcmp(name, "hostfsdir")) {
            arcemconfig_StringReplace(&pConfig->sHostFSDirectory, value);
#endif
        } else if (0 == strcmp(name, "memory")) {
            if (arcemconfig_StringToEnum(&uValue, value, memsize_labels)) {
                pConfig->eMemSize = uValue;
            } else {
                warn("Unrecognised value for %s: %s\n", name, value);
                return 0;
            }
        } else if (0 == strcmp(name, "processor")) {
            if (arcemconfig_StringToEnum(&uValue, value, processor_labels)) {
                pConfig->eProcessor = uValue;
            } else {
                warn("Unrecognised value for %s: %s\n", name, value);
                return 0;
            }
        } else {
            warn("Unknown section/name: %s, %s, %s\n", section, name, value);
            return 0;
        }
    } else if (strlen(section) == 4 && 0 == memcmp(section, "fdc", 3) &&
               section[3] >= '0' && section[3] <= '3') {
        int drive = section[3] - '0';
        if (0 == strcmp(name, "path")) {
            arcemconfig_StringReplace(&pConfig->aFloppyPaths[drive], value);
        } else {
            warn("Unknown section/name: %s, %s, %s\n", section, name, value);
            return 0;
        }
    } else if (strlen(section) == 4 && 0 == memcmp(section, "mfm", 3) &&
               section[3] >= '0' && section[3] <= '3') {
        int drive = section[3] - '0';
        if (0 == strcmp(name, "path")) {
            arcemconfig_StringReplace(&pConfig->aST506Paths[drive], value);
        } else if (0 == strcmp(name, "cylinders")) {
            pConfig->aST506DiskShapes[drive].NCyls = atoi(value);
        } else if (0 == strcmp(name, "heads")) {
            pConfig->aST506DiskShapes[drive].NHeads = atoi(value);
        } else if (0 == strcmp(name, "sectors")) {
            pConfig->aST506DiskShapes[drive].NSectors = atoi(value);
        } else if (0 == strcmp(name, "reclength")) {
            pConfig->aST506DiskShapes[drive].RecordLength = atoi(value);
        } else {
            warn("Unknown section/name: %s, %s, %s\n", section, name, value);
            return 0;
        }
    } else {
        warn("Unknown section/name: %s, %s, %s\n", section, name, value);
        return 0;  /* unknown section/name, error */
    }

    return 1;
}

/**
 * ArcemConfig_ParseConfigFile
 *
 * Parse and fill in the runtime options into the supplied configuration
 * structure from the config file.
 *
 * @param pConfig The structure to fill in
 */
void ArcemConfig_ParseConfigFile(ArcemConfig* pConfig)
{
    FILE *file;
#if defined(__riscos__)
    const char* filename = "<ArcEm$Dir>.arcem/ini";
#else
    const char* filename = "arcem.ini";
#endif
    dbug("Reading options from file %s\n", filename);

    file = fopen(filename, "r");
    if (file) {
        ini_parse_file(file, ArcemConfig_Handler, pConfig);
        fclose(file);
    } else {
        file = File_OpenAppData(filename, "r");
        if (file) {
            ini_parse_file(file, ArcemConfig_Handler, pConfig);
            fclose(file);
        }
    }
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
void ArcemConfig_ParseCommandLine(ArcemConfig *pConfig, int argc, char *argv[])
{
  unsigned int uValue;
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
#if defined(SYSTEM_riscos_single) || defined(SYSTEM_win)
    "  --display <mode> - Select display driver, 'pal' or 'std'\n"
#endif /* SYSTEM_riscos_single || SYSTEM_win */
#if defined(SYSTEM_riscos_single)
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

  /* No commandline arguments? */
  if(0 == argc) {
    /* There should always be at least 1, the program name */
#ifdef AMIGA
/* Unless we are launching from Workbench... */
	wblaunch((struct WBStartup *)argv, pConfig);
#endif
    return;
  }

  if(1 == argc) {
    /* No arguments other than the program name, no work to do */
    return;
  }

  /* We don't care about argument 0 as that's the program name */
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
    else if(0 == strcmp("--config", argv[iArgument])) {
      if(iArgument+1 < argc) { /* Is there a following argument? */
        ini_parse(argv[iArgument + 1], ArcemConfig_Handler, pConfig);
        iArgument += 2;
      } else {
        /* No argument following the --config option */
        ControlPane_Error(EXIT_FAILURE,"No argument following the --config option\n");
      }
    }
    else if(0 == strcmp("--rom", argv[iArgument])) {
      if(iArgument+1 < argc) { /* Is there a following argument? */
        arcemconfig_StringReplace(&pConfig->sRomImageName, argv[iArgument + 1]);
        iArgument += 2;
      } else {
        /* No argument following the --rom option */
        ControlPane_Error(EXIT_FAILURE,"No argument following the --rom option\n");
      }
    }
    else if (0 == strcmp("--hexcmos", argv[iArgument])) {
      if (iArgument + 1 < argc) { /* Is there a following argument? */
        arcemconfig_StringReplace(&pConfig->sCMOSFileName, argv[iArgument + 1]);
        iArgument += 2;
      }
      else {
        /* No argument following the --hexcmos option */
        ControlPane_Error(EXIT_FAILURE,"No argument following the --hexcmos option\n");
      }
    }
#if defined(EXTNROM_SUPPORT)
    else if(0 == strcmp("--extnromdir", argv[iArgument])) {
      if(iArgument+1 < argc) { /* Is there a following argument? */
        arcemconfig_StringReplace(&pConfig->sEXTNDirectory, argv[iArgument + 1]);
        iArgument += 2;
      } else {
        /* No argument following the --extnromdir option */
        ControlPane_Error(EXIT_FAILURE,"No argument following the --extnromdir option\n");
      }
    }
#endif /* EXTNROM_SUPPORT */
#if defined(HOSTFS_SUPPORT)
    else if(0 == strcmp("--hostfsdir", argv[iArgument])) {
      if(iArgument+1 < argc) { /* Is there a following argument? */
        arcemconfig_StringReplace(&pConfig->sHostFSDirectory, argv[iArgument + 1]);
        iArgument += 2;
      } else {
        /* No argument following the --hostfsdir option */
        ControlPane_Error(EXIT_FAILURE,"No argument following the --hostfsdir option\n");
      }
    }
#endif /* HOSTFS_SUPPORT */
    else if(0 == strcmp("--memory", argv[iArgument])) {
      if(iArgument+1 < argc) { /* Is there a following argument? */
        if (arcemconfig_StringToEnum(&uValue, argv[iArgument + 1], memsize_labels)) {
          pConfig->eMemSize = uValue;
          iArgument += 2;
        } else {
          ControlPane_Error(EXIT_FAILURE,"Unrecognised value '%s' to the --memory option\n", argv[iArgument + 1]);
        }

      } else {
        /* No argument following the --memory option */
        ControlPane_Error(EXIT_FAILURE,"No argument following the --memory option\n");
      }
    }
    else if(0 == strcmp("--processor", argv[iArgument])) {
      if(iArgument+1 < argc) { /* Is there a following argument? */
        if (arcemconfig_StringToEnum(&uValue, argv[iArgument + 1], processor_labels)) {
          pConfig->eProcessor = uValue;
          iArgument += 2;
        } else {
          ControlPane_Error(EXIT_FAILURE,"Unrecognised value '%s' to the --processor option\n", argv[iArgument + 1]);
        }
      } else {
        /* No argument following the --processor option */
        ControlPane_Error(EXIT_FAILURE,"No argument following the --processor option\n");
      }
    }
#if defined(SYSTEM_riscos_single) || defined(SYSTEM_win)
    else if(0 == strcmp("--display", argv[iArgument])) {
      if(iArgument+1 < argc) { /* Is there a following argument? */
        if(0 == strcmp("pal", argv[iArgument + 1])) {
          pConfig->eDisplayDriver = DisplayDriver_Palettised;
          iArgument += 2;
        }
        else if(0 == strcmp("std", argv[iArgument + 1])) {
          pConfig->eDisplayDriver = DisplayDriver_Standard;
          iArgument += 2;
        }
        else {
          ControlPane_Error(EXIT_FAILURE,"Unrecognised value '%s' to the --display option\n", argv[iArgument + 1]);
        }
      } else {
        ControlPane_Error(EXIT_FAILURE,"No argument following the --display option\n");
      }
    } else if(0 == strcmp("--noaspect",argv[iArgument])) {
      pConfig->bAspectRatioCorrection = false;
      iArgument += 1;
    } else if(0 == strcmp("--noupscale",argv[iArgument])) {
      pConfig->bUpscale = false;
      iArgument += 1;
    }
#endif /* SYSTEM_riscos_single || SYSTEM_win */
#if defined(SYSTEM_riscos_single)
    else if(0 == strcmp("--rbswap",argv[iArgument])) {
      pConfig->bRedBlueSwap = true;
      iArgument += 1;
    } else if(0 == strcmp("--nolowcolour",argv[iArgument])) {
      pConfig->bNoLowColour = true;
      iArgument += 1;
    } else if(0 == strcmp("--minres",argv[iArgument])) {
      if(iArgument+2 < argc) {
        pConfig->iMinResX = atoi(argv[iArgument+1]);
        pConfig->iMinResY = atoi(argv[iArgument+2]);
        iArgument += 3;
      } else {
        ControlPane_Error(EXIT_FAILURE,"Not enough arguments following the --minres option\n");
      }
    } else if(0 == strcmp("--lcdres",argv[iArgument])) {
      if(iArgument+2 < argc) {
        pConfig->iLCDResX = atoi(argv[iArgument+1]);
        pConfig->iLCDResY = atoi(argv[iArgument+2]);
        iArgument += 3;
      } else {
        ControlPane_Error(EXIT_FAILURE,"Not enough arguments following the --lcdres option\n");
      }
    } else if(0 == strcmp("--menukeys",argv[iArgument])) {
      if(iArgument+2 < argc) {
        pConfig->iTweakMenuKey1 = atoi(argv[iArgument+1]);
        pConfig->iTweakMenuKey2 = atoi(argv[iArgument+2]);
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
  size_t sLen;
  assert(sInput);

  sLen = strlen(sInput);
  sNew = malloc(sLen + 1); /* +1 is the string null terminator */
  if(sNew) {
    memcpy(sNew, sInput, sLen);
    sNew[sLen] = 0;
  }

  return sNew;
}

static void arcemconfig_StringReplace(char **sPtr, const char *sInput)
{
    char *sNew = arcemconfig_StringDuplicate(sInput);

    /* Only replace the romname if we successfully allocated a new string */
    if (sNew) {
        /* Free up the old one */
        free(*sPtr);

        *sPtr = sNew;
    }
}

static bool arcemconfig_StringToEnum(unsigned int *uPtr, const char *sInput, const ArcemConfig_Label *labels) {
    while (labels->name != NULL) {
        if (0 == strcmp(sInput, labels->name)) {
            *uPtr = labels->value;
            return true;
        }

        labels++;
    }
    return false;
}
