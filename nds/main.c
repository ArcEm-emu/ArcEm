#include "../armdefs.h"
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
	ArcemConfig_Result result = Result_Continue;
	ARMul_State *state = NULL;
	int exit_code;

#if defined(EXTNROM_SUPPORT)
	if (!nitroFSInit(NULL)) {
		ControlPane_Error(false, "Failed to initialise filesystem");
		return EXIT_FAILURE;
	}
#else
	if (!fatInitDefault()) {
		ControlPane_Error(false, "Failed to initialise filesystem");
		return EXIT_FAILURE;
	}
#endif

	/* Setup the default values for the config system */
	if (result == Result_Continue)
		result = ArcemConfig_SetupDefaults(&hArcemConfig);

	/* Parse the config file to overrule the defaults */
	if (result == Result_Continue)
		result = ArcemConfig_ParseConfigFile(&hArcemConfig);

	/* Parse any commandline arguments given to the program
	   to overrule the defaults */
	if (result == Result_Continue)
		result = ArcemConfig_ParseCommandLine(&hArcemConfig, argc, argv);

	if (result != Result_Continue) {
		ArcemConfig_Free(&hArcemConfig);
		return (result == Result_Success ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	if (isDSiMode()) {
		if (hArcemConfig.eMemSize > MemSize_4M)
			hArcemConfig.eMemSize = MemSize_4M;
	} else {
		if (hArcemConfig.eMemSize > MemSize_1M)
			hArcemConfig.eMemSize = MemSize_1M;
	}

	/* Initialise */
	state = ARMul_NewState(&hArcemConfig);
	if (!state) {
		ArcemConfig_Free(&hArcemConfig);
		return EXIT_FAILURE;
	}

	/* Execute */
	exit_code = ARMul_DoProg(state);

	/* Finalise */
	ARMul_FreeState(state);
	ArcemConfig_Free(&hArcemConfig);

	return exit_code;
}
