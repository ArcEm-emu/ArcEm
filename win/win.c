#include <windows.h>
#include <stdio.h>
#include "win.h"
#include "gui.h"

#define MonitorWidth 800
#define MonitorHeight 600

#define NR_THREADS (0x1000)

HBITMAP hbmp = NULL;
HBITMAP cbmp = NULL;

//BITMAPFILEHEADER fhdr;
BITMAPINFO pbmi;
BITMAPINFO cbmi;

static char	szWindowClass[32]="GenericClass";

int xSize, ySize;

ATOM			MyRegisterClass(HINSTANCE hInstance);
BOOL			InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);

MSG msg;
HANDLE hInst;
HWND mainWin;
DWORD tid;

//char *dibbmp;
WORD *dibbmp;
WORD *curbmp;

int nVirtKey;
int lKeyData;
int nKeyStat;
int keyF = 0;

int nMouseX;
int nMouseY;
int mouseMF = 0;

int nButton = 0;
int buttF = 0;

static DWORD threadWindow(LPVOID param)
{
//	xSize = w;
//	ySize = h;

	hInst = GetModuleHandle(NULL);

	MyRegisterClass((HINSTANCE)hInst);

	// Perform application initialization:
	if (!InitInstance ((HINSTANCE) hInst, SW_SHOWDEFAULT)) {
		return FALSE;
	}


	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX); 

	wcex.style		= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= (WNDPROC)WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon		= LoadIcon(hInstance, "GuiIcon");
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= "GuiMenu";
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= NULL; //LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL);

	return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HANDLE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   mainWin = CreateWindow( szWindowClass,
			"Archimedes emulator",
			WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, //WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, 
			CW_USEDEFAULT,
			xSize+GetSystemMetrics(SM_CXFIXEDFRAME)*2,
			ySize+GetSystemMetrics(SM_CYFIXEDFRAME)*2
			+GetSystemMetrics(SM_CYCAPTION)
			+GetSystemMetrics(SM_CYMENU),
			NULL,
			NULL,
			hInstance,
			NULL);

   if (!mainWin)
   {
      return FALSE;
   }

   ShowWindow(mainWin, nCmdShow);
   UpdateWindow(mainWin);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, unsigned, WORD, LONG)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;
//	char *szHello = "Unregistred demo!";
//	LoadString(hInst, IDS_HELLO, szHello, MAX_LOADSTRING);

	switch (message) 
	{
		case WM_COMMAND:
			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections:
			switch (wmId)
			{
				case IDM_ABOUT:
//				   DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				   break;
				case IDM_COPY:
				   break;
				case IDM_EXIT:
        			   DestroyWindow(hWnd);
        			   exit(0);
				   break;
				default:
				   return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;
		case WM_PAINT: {
//			RECT rt;
			HDC hsrc;
			HDC hsrc1;

			hdc = BeginPaint(hWnd, &ps);
			// TODO: Add any drawing code here...
//			GetClientRect(hWnd, &rt);
//			DrawText(hdc, szHello, strlen(szHello), &rt, DT_CENTER);

			hsrc=CreateCompatibleDC(hdc);
			SelectObject(hsrc,hbmp);
			BitBlt(hdc,0,0,xSize,ySize,hsrc,0,0,SRCCOPY);
			DeleteDC(hsrc);

			hsrc1=CreateCompatibleDC(hdc);
			SelectObject(hsrc1,cbmp);
			BitBlt(hdc,rMouseX,rMouseY,32,rMouseHeight,hsrc1,0,0,SRCCOPY);
			DeleteDC(hsrc1);

			EndPaint(hWnd, &ps);
			};
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			exit(0);
			break;
		case WM_KEYDOWN:
			nVirtKey = (TCHAR) wParam;    // character code 
//			if (nVirtKey == VK_SHIFT) {
//				nVirtKey = GetKeyState(nVirtKey);
//			}
			lKeyData = lParam;              // key data 
			nKeyStat = 0;
			keyF = 1;
			break;
		case WM_KEYUP:
			nVirtKey = (TCHAR) wParam;    // character code 
//			if (nVirtKey == VK_SHIFT) {
//				nVirtKey = GetKeyState(nVirtKey);
//			}
			lKeyData = lParam;              // key data 
			nKeyStat = 1;
			keyF = 1;
			break;
		case WM_MOUSEMOVE:
			if (mouseMF == 0) {
			  nMouseX = LOWORD(lParam);  // horizontal position of cursor 
			  nMouseY = HIWORD(lParam);  // vertical position of cursor 
			}
			mouseMF = 1;
			break;
		case WM_LBUTTONDOWN:
			nButton = 0x00;
			buttF = 1;
			break;
		case WM_MBUTTONDOWN:
			nButton = 0x01;
			buttF = 1;
			break;
		case WM_RBUTTONDOWN:
			nButton = 0x02;
			buttF = 1;
			break;
		case WM_LBUTTONUP:
			nButton = 0x80;
			buttF = 1;
			break;
		case WM_MBUTTONUP:
			nButton = 0x81;
			buttF = 1;
			break;
		case WM_RBUTTONUP:
			nButton = 0x82;
			buttF = 1;
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

int createWindow(int x, int y)
{

   xSize = x;
   ySize = y;

//   memset(&fhdr, 0, sizeof(fhdr));
   memset(&pbmi, 0, sizeof(BITMAPINFOHEADER));
   memset(&cbmi, 0, sizeof(BITMAPINFOHEADER));

//   fhdr.bfType = 'MB';
//   fhdr.bfSize = sizeof(BITMAPINFOHEADER) + sizeof(BITMAPFILEHEADER)
//		  + x * y * 2 + 0;
//   fhdr.bfOffBits = sizeof(BITMAPINFOHEADER) + sizeof(BITMAPFILEHEADER)
//		  + 0;

   pbmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
   pbmi.bmiHeader.biWidth = x;
   pbmi.bmiHeader.biHeight = y;
   pbmi.bmiHeader.biCompression = BI_RGB; //0;
   pbmi.bmiHeader.biPlanes = 1;
   pbmi.bmiHeader.biSizeImage = 0; //wic->getWidth()*wic->getHeight()*scale;
   pbmi.bmiHeader.biBitCount = 16; //24;
   pbmi.bmiHeader.biXPelsPerMeter=0;
   pbmi.bmiHeader.biYPelsPerMeter=0;
   pbmi.bmiHeader.biClrUsed=0;
   pbmi.bmiHeader.biClrImportant=0;
   hbmp=CreateDIBSection(NULL,&pbmi,DIB_RGB_COLORS,(void **)&dibbmp,NULL,0);

   cbmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
   cbmi.bmiHeader.biWidth = 32;
   cbmi.bmiHeader.biHeight = y;
   cbmi.bmiHeader.biCompression = BI_RGB; //0;
   cbmi.bmiHeader.biPlanes = 1;
   cbmi.bmiHeader.biSizeImage = 0; //wic->getWidth()*wic->getHeight()*scale;
   cbmi.bmiHeader.biBitCount = 16; //24;
   cbmi.bmiHeader.biXPelsPerMeter=0;
   cbmi.bmiHeader.biYPelsPerMeter=0;
   cbmi.bmiHeader.biClrUsed=0;
   cbmi.bmiHeader.biClrImportant=0;
   cbmp=CreateDIBSection(NULL,&cbmi,DIB_RGB_COLORS,(void **)&curbmp,NULL,0);

   CreateThread(NULL,
	     16384,
	     (LPTHREAD_START_ROUTINE)threadWindow,
	     NULL,
	     0,
	     &tid);
}

int updateDisplay()
{
/*
  RECT r;
  int w, h;

  w = rMouseX+GetSystemMetrics(SM_CXFIXEDFRAME);
  h = rMouseY+GetSystemMetrics(SM_CYFIXEDFRAME)
	+GetSystemMetrics(SM_CYCAPTION)
	+GetSystemMetrics(SM_CYMENU);

  GetWindowRect(hWnd, &r);
  SetCursorPos(r.left+w, r.top+h);
 */
  InvalidateRect(mainWin,NULL,0);
  UpdateWindow(mainWin);
}

int resizeWindow(int hWidth, int hHeight)
{
  RECT r;

  int w, h;

  if (hWidth <= 0 || hWidth > xSize) return 0;
  if (hHeight <= 0 || hHeight > ySize) return 0;

  if (GetWindowRect(mainWin, &r) != TRUE) return 0;

  w = hWidth+GetSystemMetrics(SM_CXFIXEDFRAME)*2;
  h = hHeight+GetSystemMetrics(SM_CYFIXEDFRAME)*2
	+GetSystemMetrics(SM_CYCAPTION)
	+GetSystemMetrics(SM_CYMENU);

  MoveWindow(
	mainWin,
	r.left,
	r.top,
	w,
	h,
	TRUE
  );
//  r.right = r.left+w;
//  r.bottom = r.top+h;
//  ClipCursor(&r);
//  SetCursorPos(r.left+w/2, r.top+h/2);
}
