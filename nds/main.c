#include "../dagstandalone.h"
#include "../prof.h"

#include "../arch/ArcemConfig.h"
#include "../arch/ControlPane.h"

#include <nds.h>
#include <filesystem.h>

int main(int argc,char *argv[]) {
	Prof_Init();

	if (!nitroFSInit(NULL)) {
		ControlPane_Error(EXIT_FAILURE, "Failed to initialise filesystem");
	}

	// Setup the default values for the config system
	ArcemConfig_SetupDefaults();

	// Parse any commandline arguments given to the program
	// to overrule the defaults
	ArcemConfig_ParseCommandLine(argc, argv);

	if (isDSiMode()) {
		if (hArcemConfig.eMemSize > MemSize_4M)
			hArcemConfig.eMemSize = MemSize_4M;
	} else {
		if (hArcemConfig.eMemSize > MemSize_1M)
			hArcemConfig.eMemSize = MemSize_1M;
	}

	dagstandalone();

	return EXIT_SUCCESS;
}
