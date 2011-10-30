/* ArcEm ARexx support code
 * Chris Young 2006,8
 * http://www.unsatisfactorysoftware.co.uk
 */

#include "platform.h"
#include "arexx.h"
#include <reaction/reaction_macros.h>
#include "../arch/fdc1772.h"
#include <string.h>

struct Library *ARexxBase;
struct ARexxIFace *IARexx;

Object *arexx_obj = NULL;

enum
{
	RX_QUIT=0,
	RX_FLOPPY,
	RX_LED,
};

STATIC VOID rx_quit(struct ARexxCmd *, struct RexxMsg *);
STATIC VOID rx_floppy(struct ARexxCmd *, struct RexxMsg *);
STATIC VOID rx_led(struct ARexxCmd *, struct RexxMsg *);

STATIC struct ARexxCmd Commands[] =
{
	{"QUIT",RX_QUIT,rx_quit,NULL, 		0, 	NULL, 	0, 	0, 	NULL },
	{"FLOPPY",RX_FLOPPY,rx_floppy,"DRIVE/A/N,FILE", 		0, 	NULL, 	0, 	0, 	NULL },
	{"LED",RX_LED,rx_led,"LOCK/A", 		0, 	NULL, 	0, 	0, 	NULL },
	{ NULL, 		0, 				NULL, 		NULL, 		0, 	NULL, 	0, 	0, 	NULL }
};

void ARexx_Init()
{
	if((ARexxBase = OpenLibrary((char *)&"arexx.class",51)))
	{
		if(IARexx = (struct ARexxIFace *)GetInterface(ARexxBase,(char *)&"main",1,NULL))
		{
		
			arexx_obj = ARexxObject,
					AREXX_HostName,"ARCEM",
					AREXX_Commands,Commands,
					AREXX_NoSlot,TRUE,
					AREXX_ReplyHook,NULL,
					AREXX_DefExtension,"arcem",
					End;	
		}
	}

	arexx_quit = FALSE;

}

void ARexx_Handle()
{
	RA_HandleRexx(arexx_obj);
}

void ARexx_Cleanup()
{
	if(arexx_obj)
		DisposeObject(arexx_obj);

	if(IARexx)
	{
		DropInterface((struct Interface *)IARexx);
		CloseLibrary(ARexxBase);
	}
}

STATIC VOID rx_quit(struct ARexxCmd *cmd, struct RexxMsg *rxm __attribute__((unused)))
{
	arexx_quit = TRUE;
}

STATIC VOID rx_floppy(struct ARexxCmd *cmd, struct RexxMsg *rxm __attribute__((unused)))
{
	long drv = 0;
	char *err;

	drv = *(long *)cmd->ac_ArgList[0];
	FDC_EjectFloppy(drv);

	if(cmd->ac_ArgList[1])
	{
		err = FDC_InsertFloppy(drv,(char *)cmd->ac_ArgList[1]);

		if(err)
		{
			cmd->ac_RC = 1;
		}
	}
}

STATIC VOID rx_led(struct ARexxCmd *cmd, struct RexxMsg *rxm __attribute__((unused)))
{
/* Not implemented yet
	(KBD.Leds & 1) Caps
	(KBD.Leds & 2) Num
	(KBD.Leds & 4) Scroll
*/
}

