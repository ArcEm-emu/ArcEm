#define ALL_REACTION_CLASSES
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/requester.h>
#include <proto/asl.h>
#include <proto/utility.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAD_BU1 1
#define GAD_BU2 2
#define GAD_BU3 3
#define GAD_BU4 4
#define GAD_BU5 5
#define GAD_BU6 6

enum
{
	REXX_QUIT
};

STATIC VOID reply_callback(struct Hook *, Object *, struct RexxMsg *);
STATIC VOID rexx_quit (struct ARexxCmd *, struct RexxMsg *);

BOOL running = TRUE;

STATIC UBYTE systemDate[32];

STATIC struct Hook reply_hook;

STATIC struct ARexxCmd Commands[] =
{
	{"QUIT", REXX_QUIT, rexx_quit, NULL, 0, NULL, 0, 0, NULL}
};

int amiga_file_request(char *file)
{
	int sel=0;
	struct FileRequester *freq;

	if((freq = (struct FileRequester
*)IAsl->AllocAslRequest(ASL_FileRequest,NULL)))
	{
	if(IAsl->AslRequestTags(freq,
		ASLFR_TitleText,"ArcEm Control",
		TAG_DONE))
	{
	strlcpy(file,freq->fr_Drawer,1024);
	IDOS->AddPart(file,freq->fr_File,1024);
	sel=1;
	}
	IAsl->FreeAslRequest(freq);
	}
	return sel;
}

uint32 strreq(STRPTR rbuf,ULONG rbufmax)
{
	Object *requester;
	uint32 result = 0;

	requester = IIntuition->NewObject(IRequester->REQUESTER_GetClass(),
NULL,
	REQ_Type,		REQTYPE_STRING,
	REQ_TitleText,		"ArcEm Control",
	REQ_BodyText,		"What string would you like to add?",
	REQ_EvenButtons,		TRUE,
	REQ_Image,		REQIMAGE_QUESTION,
	REQS_Buffer,		rbuf,
	REQS_MaxChars,		rbufmax,
	TAG_DONE);

	if (requester)
	{
		result = IIntuition->IDoMethod( requester, RM_OPENREQ, NULL, NULL,
NULL );
		IIntuition->DisposeObject(requester);
	}

	return(result);
}

int main(void)
{
	struct Screen *screen = NULL;
	Object *arexx_obj;
	char *buf,*temp;
	BPTR in,out;

	if (!ButtonBase)
		return RETURN_FAIL;

	arexx_obj = ARexxObject,
		AREXX_HostName, "ARCEMPANEL",
		AREXX_Commands, Commands,
		AREXX_NoSlot, TRUE,
		AREXX_ReplyHook, &reply_hook,
	End;

	if (arexx_obj)
	{
		Object *win_obj;

		screen = IIntuition->LockPubScreen("ArcEm");

		win_obj = WindowObject,
			WA_Title, "ArcEm Control",
			WA_DragBar, TRUE,
			WA_CloseGadget, TRUE,
			WA_DepthGadget, TRUE,
			WA_Activate, TRUE,
			WA_PubScreen,screen,
			WINDOW_Position, WPOS_CENTERSCREEN,
			WINDOW_ParentGroup, LayoutObject,
			LAYOUT_AddChild,  VLayoutObject,
				LAYOUT_Label, "Drive 0",
				LAYOUT_AddChild, Button("Insert Disc", GAD_BU1),
				LAYOUT_AddChild, Button("Eject Disc", GAD_BU2),
			End,
			CHILD_WeightedHeight,0,
			LAYOUT_AddChild,  VLayoutObject,
				LAYOUT_Label, "Drive 1",
				LAYOUT_AddChild, Button("Insert Disc", GAD_BU5),
				LAYOUT_AddChild, Button("Eject Disc", GAD_BU6),
			End,
			CHILD_WeightedHeight,0,
			LAYOUT_AddChild,  VLayoutObject,
				LAYOUT_Label, " ",
				LAYOUT_AddChild, Button("ARexx", GAD_BU4),
				LAYOUT_AddChild, Button("Quit", GAD_BU3),
			End,
			CHILD_WeightedHeight,0,
		LayoutEnd,
		EndWindow;

		IIntuition->UnlockPubScreen(NULL,screen);

		if (win_obj)
		{
			struct Window *window;

			window = (struct Window *)RA_OpenWindow(win_obj);

			if (window)
			{
				ULONG wnsig = 0, rxsig = 0, signal, result, Code;

				reply_hook.h_Entry = (HOOKFUNC)reply_callback;
				reply_hook.h_SubEntry = NULL;
				reply_hook.h_Data = NULL;

				IIntuition->GetAttr(WINDOW_SigMask, win_obj, &wnsig);
				IIntuition->GetAttr(AREXX_SigMask, arexx_obj, &rxsig);
				BPTR in = IDOS->Open("NIL:",MODE_OLDFILE);
				BPTR out = IDOS->Open("NIL:",MODE_NEWFILE);

				do
				{
					signal = IExec->Wait(wnsig | rxsig | SIGBREAKF_CTRL_C);

					if (signal & rxsig)
						RA_HandleRexx(arexx_obj);

					if (signal & wnsig)
					{
						while ((result = RA_HandleInput(win_obj, &Code)) != WMHI_LASTMSG)
						{
							switch (result & WMHI_CLASSMASK)
							{
								case WMHI_CLOSEWINDOW:
									running = FALSE;
									break;

								case WMHI_GADGETUP:
									switch(result & WMHI_GADGETMASK)
									{
										case GAD_BU1:
											if(buf = IExec->AllocVec(1024,MEMF_CLEAR))
											{
												amiga_file_request(buf);
												temp = IUtility->ASPrintf("FLOPPY 0  \"%s\"",buf);
												IExec->FreeVec(buf);
												IIntuition->IDoMethod(arexx_obj, AM_EXECUTE, temp, "ARCEM", NULL, NULL, NULL, NULL);
												IExec->FreeVec(temp);
											}
											break;
										case GAD_BU2:
											if(buf = IExec->AllocVec(1024,MEMF_CLEAR))
											{
												temp = IUtility->ASPrintf("FLOPPY 0",buf);
												IExec->FreeVec(buf);
												IIntuition->IDoMethod(arexx_obj, AM_EXECUTE, temp, "ARCEM", NULL, NULL, NULL, NULL);
												IExec->FreeVec(temp);
											}
											break;
										case GAD_BU3:
											if(buf = IExec->AllocVec(1024,MEMF_CLEAR))
											{
												temp = IUtility->ASPrintf("QUIT",buf);
												IExec->FreeVec(buf);
												IIntuition->IDoMethod(arexx_obj, AM_EXECUTE, temp, "ARCEM", NULL, NULL, NULL, NULL);
												IExec->FreeVec(temp);
											}
											break;
										case GAD_BU4:
											if(buf = IExec->AllocVec(1024,MEMF_CLEAR))
											{
												amiga_file_request(buf);
												temp = IUtility->ASPrintf("rx  \"%s\"",buf);
												IExec->FreeVec(buf);
												in = IDOS->Open("NIL:",MODE_OLDFILE);
												out = IDOS->Open("NIL:",MODE_NEWFILE);
												IDOS->SystemTags(temp,SYS_Asynch,TRUE, SYS_Input,in, SYS_Output,out, NP_Name,"Panel Launched Process",TAG_DONE);
												IExec->FreeVec(temp);
											}
											break;
										case GAD_BU5:
											if(buf = IExec->AllocVec(1024,MEMF_CLEAR))
											{
												amiga_file_request(buf);
												temp = IUtility->ASPrintf("FLOPPY 1  \"%s\"",buf);
												IExec->FreeVec(buf);
												IIntuition->IDoMethod(arexx_obj, AM_EXECUTE, temp, "ARCEM", NULL, NULL, NULL, NULL);
												IExec->FreeVec(temp);
											}
											break;
										case GAD_BU6:
											if(buf = IExec->AllocVec(1024,MEMF_CLEAR))
											{
												temp = IUtility->ASPrintf("FLOPPY 1",buf);
												IExec->FreeVec(buf);
												IIntuition->IDoMethod(arexx_obj, AM_EXECUTE, temp, "ARCEM", NULL, NULL, NULL, NULL);
												IExec->FreeVec(temp);
											}
											break;
									}
									break;

								default:
									break;
								}
							}
						}

					if (signal & SIGBREAKF_CTRL_C)					
					{
						running = FALSE;
					}
				}
				while (running);
			}
			else
				IDOS->Printf ("Could not open the window.\n");

			IIntuition->DisposeObject(win_obj);
		}
		else
			IDOS->Printf("Could not create the window object.\n");

		IIntuition->DisposeObject(arexx_obj);
	}
	else
		IDOS->Printf("Could not create the ARexx host.\n");

	return RETURN_OK;
}

STATIC VOID reply_callback(struct Hook *hook __attribute__((unused)), Object *o __attribute__((unused)), struct RexxMsg *rxm)
{
}
STATIC VOID rexx_quit( struct ARexxCmd *ac, struct RexxMsg *rxm __attribute__((unused)))
{
	running = FALSE;
}

