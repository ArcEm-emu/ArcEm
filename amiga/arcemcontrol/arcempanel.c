#define ALL_REACTION_CLASSES
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/requester.h>
#include <proto/asl.h>
#include <proto/utility.h>
#include <proto/icon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	GAD_BU1,
	GAD_BU2,
	GAD_BU3,
	GAD_BU4,
	GAD_BU5,
	GAD_BU6
};

enum
{
	REXX_QUIT
};

STATIC VOID reply_callback(struct Hook *, Object *, struct RexxMsg *);
STATIC VOID rexx_quit (struct ARexxCmd *, struct RexxMsg *);

BOOL running = TRUE;
ULONG xpos=0;
ULONG ypos=0;
int centrewin = WPOS_CENTERSCREEN;

STATIC struct Hook reply_hook;

STATIC struct ARexxCmd Commands[] =
{
	{"QUIT", REXX_QUIT, rexx_quit, NULL, 0, NULL, 0, 0, NULL}
};

void amiga_read_tooltypes(struct WBStartup *WBenchMsg)
{
	struct WBArg *wbarg;
	char i;
	int olddir;
	struct DiskObject *dobj;
	STRPTR *toolarray;
	char *s;

	for(i=0,wbarg=WBenchMsg->sm_ArgList;i<WBenchMsg->sm_NumArgs;i++,wbarg++)
	{
	olddir =-1;
	if((wbarg->wa_Lock)&&(*wbarg->wa_Name))
	olddir = IDOS->CurrentDir(wbarg->wa_Lock);

	if((*wbarg->wa_Name) && (dobj=IIcon->GetIconTags(wbarg->wa_Name,NULL)))
	{
	toolarray = dobj->do_ToolTypes;

	if((s = IIcon->FindToolType(toolarray,"XPOS")))
	{
		xpos=atoi(s);
			centrewin=0;
	}

	if((s = IIcon->FindToolType(toolarray,"YPOS")))
	{
		ypos=atoi(s);
			centrewin=0;
	}

	IIcon->FreeDiskObject(dobj);
	}

	if(olddir !=-1) IDOS->CurrentDir(olddir);
	}
}

int amiga_file_request(char *file,struct Screen *screen)
{
	int sel=0;
	struct FileRequester *freq;

	if((freq = (struct FileRequester
*)IAsl->AllocAslRequest(ASL_FileRequest,NULL)))
	{
	if(IAsl->AslRequestTags(freq,
		ASLFR_TitleText,"ArcEm Control",
		ASLFR_Screen,screen,
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

uint32 strreq(STRPTR rbuf,ULONG rbufmax,struct Screen *screen)
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
screen );
		IIntuition->DisposeObject(requester);
	}

	return(result);
}

int main(int argc,char **argv)
{
	struct Screen *screen = NULL;
	Object *arexx_obj;
	char *buf,*temp;
	BPTR in,out;

	struct HintInfo helpstrs[] =
	{
	{-1,-1,0,0}
	};


	if(0 == argc) amiga_read_tooltypes((struct WBStartup *)argv);

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

		if(!(screen = IIntuition->LockPubScreen("ArcEm")))
		{
			screen = IIntuition->LockPubScreen(NULL);
		}

		win_obj = WindowObject,
			WA_Title, "ArcEm Control",
			WA_DragBar, TRUE,
			WA_CloseGadget, TRUE,
			WA_DepthGadget, TRUE,
			WA_Activate, TRUE,
			WA_PubScreen,screen,
			WA_Top,ypos,
			WA_Left,xpos,
			WINDOW_GadgetHelp,TRUE,
			WINDOW_HintInfo, &helpstrs,
			WINDOW_Position, centrewin,
			WINDOW_ParentGroup, LayoutObject,
			LAYOUT_AddChild,  VLayoutObject,
				LAYOUT_Label, "Drive 0",
				LAYOUT_AddChild, Button("Insert Disc", GAD_BU1),
				CHILD_MinHeight, 28,
				LAYOUT_AddChild, Button("Eject Disc", GAD_BU2),
				CHILD_MinHeight, 28,
			End,
			CHILD_WeightedHeight,0,
			LAYOUT_AddChild,  VLayoutObject,
				LAYOUT_Label, "Drive 1",
				LAYOUT_AddChild, Button("Insert Disc", GAD_BU5),
				CHILD_MinHeight, 28,
				LAYOUT_AddChild, Button("Eject Disc", GAD_BU6),
				CHILD_MinHeight, 28,
			End,
			CHILD_WeightedHeight,0,
			LAYOUT_AddChild,  VLayoutObject,
				LAYOUT_Label, " ",
				LAYOUT_AddChild, Button("ARexx", GAD_BU4),
				CHILD_MinHeight, 28,
				LAYOUT_AddChild, Button("_Quit", GAD_BU3),
				CHILD_MinHeight, 28,
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
				BPTR in = 0;
				BPTR out = 0;

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
												amiga_file_request(buf,screen);
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
												amiga_file_request(buf,screen);
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
												amiga_file_request(buf,screen);
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

