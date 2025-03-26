#include "../dagstandalone.h"
#include "../prof.h"

#include "../arch/ArcemConfig.h"
#include "../arch/ControlPane.h"

#include <nds.h>
#if defined(EXTNROM_SUPPORT)
#include <filesystem.h>
#else
#include <fat.h>
#endif

static ArcemConfig hArcemConfig;

int main(int argc,char *argv[]) {
	Prof_Init();

#if defined(EXTNROM_SUPPORT)
	if (!nitroFSInit(NULL)) {
		ControlPane_Error(EXIT_FAILURE, "Failed to initialise filesystem");
	}
#else
	if (!fatInitDefault()) {
		ControlPane_Error(EXIT_FAILURE, "Failed to initialise filesystem");
	}
#endif

	// Setup the default values for the config system
	ArcemConfig_SetupDefaults(&hArcemConfig);

	// Parse the config file to overrule the defaults
	ArcemConfig_ParseConfigFile(&hArcemConfig);

	// Parse any commandline arguments given to the program
	// to overrule the defaults
	ArcemConfig_ParseCommandLine(&hArcemConfig, argc, argv);

	if (isDSiMode()) {
		if (hArcemConfig.eMemSize > MemSize_4M)
			hArcemConfig.eMemSize = MemSize_4M;
	} else {
		if (hArcemConfig.eMemSize > MemSize_1M)
			hArcemConfig.eMemSize = MemSize_1M;
	}

	dagstandalone(&hArcemConfig);

	return EXIT_SUCCESS;
}
