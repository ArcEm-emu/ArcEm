/* ArcEm ARexx support code
 * Chris Young 2006
 * http://www.unsatisfactorysoftware.co.uk
 */

#include "platform.h"
#include "arexx.h"
#include <reaction/reaction_macros.h>

struct Library *ARexxBase;
struct ARexxIFace *IARexx;

Object *arexx_obj = NULL;

enum
{
	RX_QUIT=0,
	RX_FLOPPY,
	RX_LED,
};

STATIC VOID reply_callback(struct Hook *, Object *, struct RexxMsg *);
STATIC VOID rx_quit(struct ARexxCmd *, struct RexxMsg *);
STATIC VOID rx_floppy(struct ARexxCmd *, struct RexxMsg *);
STATIC VOID rx_led(struct ARexxCmd *, struct RexxMsg *);

STATIC CONST struct ARexxCmd Commands[] =
{
	{"QUIT",RX_QUIT,rx_quit,NULL, 		0, 	NULL, 	0, 	0, 	NULL },
	{"FLOPPY",RX_FLOPPY,rx_floppy,"DRIVE/A/N,FILE", 		0, 	NULL, 	0, 	0, 	NULL },
	{"LED",RX_LED,rx_led,"LOCK/A", 		0, 	NULL, 	0, 	0, 	NULL },
	{ NULL, 		0, 				NULL, 		NULL, 		0, 	NULL, 	0, 	0, 	NULL }
};

void ARexx_Init()
{
	struct Hook reply_hook;

	if((ARexxBase = IExec->OpenLibrary((char *)&"arexx.class",51)))
	{
		if(IARexx = (struct ARexxIFace *)IExec->GetInterface(ARexxBase,(char *)&"main",1,NULL))
		{
		
			if((arexx_obj = ARexxObject,
					AREXX_HostName,"ARCEM",
					AREXX_Commands,Commands,
					AREXX_NoSlot,TRUE,
					AREXX_ReplyHook,&reply_hook,
					AREXX_DefExtension,"arcem",
					End));	
			{
				reply_hook.h_Entry = (HOOKFUNC)reply_callback;
				reply_hook.h_SubEntry = NULL;
				reply_hook.h_Data = NULL;
//		IIntuition->GetAttr(AREXX_SigMask, arexx_obj, &rxsig);
			}
		}
	}

	arexx_quit = FALSE;

}

void ARexx_Handle()
{
	RA_HandleRexx(arexx_obj);
}

void ARexx_Execute(STRPTR filename)
{
	char cmdline[1024];

//	IIntuition->IDoMethod(arexx_obj, AM_EXECUTE, filename, NULL, NULL, NULL, NULL, NULL);

	strcpy(cmdline,"run rx ");
	strcat(cmdline,filename);

	IDOS->SystemTags(cmdline,TAG_DONE);

}

void ARexx_Cleanup()
{
	if(arexx_obj)
		IIntuition->DisposeObject(arexx_obj);

	if(IARexx)
	{
		IExec->DropInterface((struct Interface *)IARexx);
		IExec->CloseLibrary(ARexxBase);
	}
}

STATIC VOID reply_callback(struct Hook *hook, Object *object, struct RexxMsg *msg  __attribute__((unused)))
{
	// do nothing
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

