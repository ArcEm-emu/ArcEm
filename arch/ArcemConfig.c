/*
  ArcemConfig

  (c) 2006 P Howkins

  Part of Arcem released under the GNU GPL, see file COPYING
  for details.

  ArcemConfig - Stores the running configuration of the Arcem
  emulator.
 
  See usage instructions in the ArcemConfig.h header file.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ArcemConfig.h"
#include "Version.h"


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
    "Arcem <Options>\n" \
    " Where options are one or more of the following\n" \
    "  --help - Display this message and exit\n" \
    "  --version - Display the version number and exit\n" \
    "  --memory <value> - Set the memory size of the emulator\n" \
    "     Where value is one of '256K', '512K', '1M', '2M', '4M',\n" \
    "     '8M', '12M' or '16M'\n" \
    "  --processor <value> - Set the emulated CPU\n" \
    "     Where value is one of 'ARM2', 'ARM250', 'ARM3'\n";

  // No commandline arguments?
  if(0 == argc) {
    // There should always be at least 1, the program name
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
          fprintf(stderr, "Unrecognised value to the --memory option\n");

          exit(EXIT_FAILURE);
        }

      } else {
        // No argument following the --memory option
        fprintf(stderr, "No argument following the --memory option\n");

        exit(EXIT_FAILURE);
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
          fprintf(stderr, "Unrecognised value to the --processor option\n");

          exit(EXIT_FAILURE);
        }
      } else {
        // No argument following the --processor option
        fprintf(stderr, "No argument following the --processor option\n");

        exit(EXIT_FAILURE);
      }
    }
    else {
      fprintf(stderr, "Unrecognised option '%s', try --help\n", argv[iArgument]);

      exit(EXIT_FAILURE);
    }
  }
}
