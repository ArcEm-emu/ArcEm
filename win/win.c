#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include "win.h"
#include "gui.h"
#include "armdefs.h"
#include "arch/ArcemConfig.h"
#include "arch/keyboard.h"

#define NR_THREADS (0x1000)

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif
#ifndef WM_XBUTTONDOWN
#define WM_XBUTTONDOWN 0x20B
#endif
#ifndef WM_XBUTTONUP
#define WM_XBUTTONUP 0x20C
#endif
#ifndef MAPVK_VSC_TO_VK_EX
#define MAPVK_VSC_TO_VK_EX 3
#endif
#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(wParam)  ((short)HIWORD(wParam))
#endif

HBITMAP hbmp = NULL;
HBITMAP cbmp = NULL;
HBITMAP mbmp = NULL;

/*BITMAPFILEHEADER fhdr;*/
BITMAPINFO *pbmi;
BITMAPINFO *cbmi;
BITMAPINFO *mbmi;

static CRITICAL_SECTION bmpCriticalSection;

static const TCHAR szWindowClass[32] = TEXT("GenericClass");


static BOOL captureMouse = FALSE;

static RECT rcClip;           /* new area for ClipCursor */

static RECT rcOldClip;        /* previous area for ClipCursor */




int xSize, ySize;

ATOM            MyRegisterClass(HINSTANCE hInstance);
BOOL            InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AboutDlgProc (HWND, UINT, WPARAM, LPARAM);

static void SelectMenuItem(HWND hWnd, ArcemConfig_DisplayDriver driver);
static bool SelectCheckbox(HWND hWnd, int wmId, bool value);

static MSG msg;
static HANDLE hInst;
static HWND mainWin;
static DWORD tid;

void *dibbmp;
void *curbmp;
void *mskbmp;

size_t dibstride;
size_t curstride;
size_t mskstride;

ArcemConfig_DisplayDriver requestedDriver;
bool requestedAspect;
bool requestedUpscale;

static DWORD WINAPI threadWindow(LPVOID param)
{
    ARMul_State* state = (ARMul_State*)param;

    hInst = GetModuleHandle(NULL);

    MyRegisterClass((HINSTANCE)hInst);

    /* Perform application initialization: */
    if (!InitInstance ((HINSTANCE) hInst, SW_SHOWDEFAULT)) {
        return FALSE;
    }

    SelectMenuItem(mainWin, CONFIG.eDisplayDriver);
    requestedAspect = SelectCheckbox(mainWin, IDM_ASPECT, CONFIG.bAspectRatioCorrection);
    requestedUpscale = SelectCheckbox(mainWin, IDM_UPSCALE, CONFIG.bUpscale);

    /* Main message loop: */
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

    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = (WNDPROC)WndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, TEXT("GuiIcon"));
    wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName  = TEXT("GuiMenu");
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm       = NULL; /* LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL); */

    return RegisterClassEx(&wcex);
}

static void insert_floppy(HWND hWnd, int drive, char *image)
{
	const char *err;

	if (FDC_IsFloppyInserted(drive)) {
		err = FDC_EjectFloppy(drive);
		fprintf(stderr, "ejecting drive %d: %s\n", drive,
		        err ? err : "ok");
	}

	err = FDC_InsertFloppy(drive, image);
	fprintf(stderr, "inserting floppy image %s into drive %d: %s\n",
	        image, drive, err ? err : "ok");

	if (err == NULL)
		EnableMenuItem(GetMenu(hWnd), IDM_EJECT0 + drive, MF_ENABLED);
	else
		EnableMenuItem(GetMenu(hWnd), IDM_EJECT0 + drive, MF_GRAYED);
}

static void OpenFloppyImageDialog(HWND hWnd, int drive) {
	OPENFILENAMEA ofn;      /* common dialog box structure */
	char szFile[260];       /* buffer for file name */

	/* Initialize OPENFILENAME */
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = mainWin;
	ofn.lpstrFile = szFile;
	/* Set lpstrFile[0] to '\0' so that GetOpenFileName does not
	   use the contents of szFile to initialize itself. */
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "ADF files (*.adf)\0*.ADF\0All\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	/* Display the Open dialog box. */
	if (GetOpenFileNameA(&ofn)==TRUE) {
		insert_floppy(hWnd, drive, szFile);
	}
}

static void EjectFloppyImage(HWND hWnd, int drive) {
	const char *err = FDC_EjectFloppy(drive);
	fprintf(stderr, "ejecting drive %d: %s\n",
	        drive, err ? err : "ok");

	EnableMenuItem(GetMenu(hWnd), IDM_EJECT0 + drive, MF_GRAYED);
}

static void SelectMenuItem(HWND hWnd, ArcemConfig_DisplayDriver driver) {
    int wmId;

    switch (driver) {
    case DisplayDriver_Palettised:
        wmId = IDM_PALDISPLAY;
        break;
    case DisplayDriver_Standard:
        wmId = IDM_STDDISPLAY;
        break;
    default:
        return;
    }

    requestedDriver = driver;
    CheckMenuRadioItem(GetMenu(hWnd), IDM_STDDISPLAY, IDM_PALDISPLAY, wmId, MF_BYCOMMAND);
}

static bool SelectCheckbox(HWND hWnd, int wmId, bool value) {
    CheckMenuItem(GetMenu(hWnd), wmId, value ? MF_CHECKED : MF_UNCHECKED);
    return value;
}

/**
 *   FUNCTION: InitInstance(HANDLE, int)
 *
 *   PURPOSE: Saves instance handle and creates main window
 *
 *   COMMENTS:
 *
 *        In this function, we save the instance handle in a global variable and
 *        create and display the main program window.
 */
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   mainWin = CreateWindow( szWindowClass,
            TEXT("Archimedes Emulator"),
            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, /* WS_OVERLAPPEDWINDOW, */
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            xSize + GetSystemMetrics(SM_CXFIXEDFRAME) * 2,
            ySize + GetSystemMetrics(SM_CYFIXEDFRAME) * 2
            + GetSystemMetrics(SM_CYCAPTION)
            + GetSystemMetrics(SM_CYMENU),
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

/**
 *  FUNCTION: WndProc(HWND, unsigned, WORD, LONG)
 *
 *  PURPOSE:  Processes messages for the main window.
 *
 *  WM_COMMAND  - process the application menu
 *  WM_PAINT    - Paint the main window
 *  WM_DESTROY  - post a quit message and return
 *
 */
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  int wmId, nVirtKey, nMouseX, nMouseY;
  PAINTSTRUCT ps;
  HDC hdc;
  ARMul_State *state = &statestr;

  switch (message)
  {
    case WM_COMMAND:
      wmId    = LOWORD(wParam);
      /* Parse the menu selections: */
      switch (wmId)
      {
        case IDM_ABOUT:
          DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, AboutDlgProc);
          break;
        case IDM_COPY:
          break;
        case IDM_OPEN0:
        case IDM_OPEN1:
        case IDM_OPEN2:
        case IDM_OPEN3:
            OpenFloppyImageDialog(hWnd, wmId - IDM_OPEN0);
            break;
        case IDM_EJECT0:
        case IDM_EJECT1:
        case IDM_EJECT2:
        case IDM_EJECT3:
            EjectFloppyImage(hWnd, wmId - IDM_EJECT0);
            break;
        case IDM_EXIT:
          DestroyWindow(hWnd);
          exit(0);
          break;
        case IDM_STDDISPLAY:
          SelectMenuItem(hWnd, DisplayDriver_Standard);
          break;
        case IDM_PALDISPLAY:
          SelectMenuItem(hWnd, DisplayDriver_Palettised);
          break;
        case IDM_ASPECT:
          requestedAspect = SelectCheckbox(mainWin, IDM_ASPECT, !CONFIG.bAspectRatioCorrection);
          break;
        case IDM_UPSCALE:
          requestedUpscale = SelectCheckbox(mainWin, IDM_UPSCALE, !CONFIG.bUpscale);
          break;
        default:
          return DefWindowProc(hWnd, message, wParam, lParam);
      }
      break;

    case WM_PAINT: {
      HDC hsrc;
      HDC hsrc1;
      HDC hsrc2;
      HDC hsrc3;
      HBITMAP ibmp;

      hdc = BeginPaint(hWnd, &ps);

      EnterCriticalSection(&bmpCriticalSection);

      if (hbmp) {
        if(rMouseHeight > 0) {
          int rWidth = xSize - (rMouseX + rMouseWidth);
          int bHeight = ySize - (rMouseY + rMouseHeight);

          hsrc = CreateCompatibleDC(hdc);
          SelectObject(hsrc, hbmp);
          SetDIBColorTable(hsrc, 0, 256, pbmi->bmiColors);

          hsrc1=CreateCompatibleDC(hdc);
          SelectObject(hsrc1, cbmp);
          SetDIBColorTable(hsrc1, 0, 4, cbmi->bmiColors);

          /* Blit the screen in multiple parts to avoid flickering.  */
          BitBlt(hdc, 0, 0, xSize, rMouseY, hsrc, 0, 0, SRCCOPY); /* top */
          BitBlt(hdc, 0, rMouseY, rMouseX, rMouseHeight, hsrc, 0, rMouseY, SRCCOPY); /* left */
          if (mbmp) {
            hsrc2 = CreateCompatibleDC(hdc);
            ibmp = CreateCompatibleBitmap(hdc, rMouseWidth, rMouseHeight);
            SelectObject(hsrc2, ibmp);

            hsrc3 = CreateCompatibleDC(hdc);
            SelectObject(hsrc3, mbmp);

            BitBlt(hsrc2, 0, 0, rMouseWidth, rMouseHeight, hsrc, rMouseX, rMouseY, SRCCOPY);
            BitBlt(hsrc2, 0, 0, rMouseWidth, rMouseHeight, hsrc3, 0, 0, SRCAND);
            BitBlt(hsrc2, 0, 0, rMouseWidth, rMouseHeight, hsrc1, 0, 0, SRCPAINT);
            BitBlt(hdc, rMouseX, rMouseY, rMouseWidth, rMouseHeight, hsrc2, 0, 0, SRCCOPY);

            DeleteDC(hsrc3);
            DeleteDC(hsrc2);
            DeleteObject(ibmp);
          } else {
            /* The mouse bitmap is already combined with the image underneath.  */
            BitBlt(hdc, rMouseX, rMouseY, rMouseWidth, rMouseHeight, hsrc1, 0, 0, SRCCOPY);
          }
          BitBlt(hdc, rMouseX + rMouseWidth, rMouseY, rWidth, rMouseHeight, hsrc, rMouseX + rMouseWidth, rMouseY, SRCCOPY); /* right */
          BitBlt(hdc, 0, rMouseY + rMouseHeight, xSize, bHeight, hsrc, 0, rMouseY + rMouseHeight, SRCCOPY); /* bottom */

          DeleteDC(hsrc1);
          DeleteDC(hsrc);
        } else {
          hsrc = CreateCompatibleDC(hdc);
          SelectObject(hsrc, hbmp);
          SetDIBColorTable(hsrc, 0, 256, pbmi->bmiColors);
          BitBlt(hdc, 0, 0, xSize, ySize, hsrc, 0, 0, SRCCOPY);
          DeleteDC(hsrc);
        }
      }

      LeaveCriticalSection(&bmpCriticalSection);

      EndPaint(hWnd, &ps);
        }
        break;


    case WM_DESTROY:
#ifdef SOUND_SUPPORT
      sound_exit();
#endif
      PostQuitMessage(0);
      exit(0);
      break;


        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            nVirtKey = (TCHAR) wParam;    /* character code */
            if (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU)
                nVirtKey = MapVirtualKey((lParam & 0xff0000) >> 16, MAPVK_VSC_TO_VK_EX);

            if (wParam == VK_ADD) {
                captureMouse = !captureMouse;
                if (captureMouse) {
                    GetClipCursor(&rcOldClip);
                    GetWindowRect(hWnd, &rcClip);
                    ClipCursor(&rcClip);
                    SetCursor(NULL);
                } else {
                    ClipCursor(&rcOldClip);
                }
                break;
            }
            ProcessKey(state, nVirtKey & 255, (int)lParam, 0);
            break;


        case WM_SYSKEYUP:
        case WM_KEYUP:
            nVirtKey = (TCHAR) wParam;    /* character code */
            if (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU)
                nVirtKey = MapVirtualKey((lParam & 0xff0000) >> 16, MAPVK_VSC_TO_VK_EX);

            ProcessKey(state, nVirtKey & 255, (int)lParam, 1);
            break;


        case WM_MOUSEMOVE:

            nMouseX = GET_X_LPARAM(lParam);  /* horizontal position of cursor */
            nMouseY = GET_Y_LPARAM(lParam);  /* vertical position of cursor */

            if (captureMouse) {
                SetCursor(NULL);
                nMouseX *= 2;
                nMouseY *= 2;
            }

            MouseMoved(state, nMouseX, nMouseY);

            break;


        case WM_LBUTTONDOWN:
            keyboard_key_changed(&KBD, ARCH_KEY_button_1, 0);
            break;


        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
            keyboard_key_changed(&KBD, ARCH_KEY_button_2, 0);
            break;


        case WM_RBUTTONDOWN:
            keyboard_key_changed(&KBD, ARCH_KEY_button_3, 0);
            break;


        case WM_LBUTTONUP:
            keyboard_key_changed(&KBD, ARCH_KEY_button_1, 1);
            break;


        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
            keyboard_key_changed(&KBD, ARCH_KEY_button_2, 1);
            break;


        case WM_RBUTTONUP:
            keyboard_key_changed(&KBD, ARCH_KEY_button_3, 1);
            break;

        case WM_MOUSEWHEEL:
            {
                int iMouseWheelValue = GET_WHEEL_DELTA_WPARAM(wParam);

                if(iMouseWheelValue > 0) {
                    /* Fire our fake button_4 wheelup event, this'll get picked up
                       by the scrollwheel module in RISC OS */
                    keyboard_key_changed(&KBD, ARCH_KEY_button_4, 1);
                } else if(iMouseWheelValue < 0) {
                    /* Fire our fake button_5 wheeldown event, this'll get picked up
                       by the scrollwheel module in RISC OS */
                    keyboard_key_changed(&KBD, ARCH_KEY_button_5, 1);
                }
            }
            break;

        case WM_SETCURSOR:

            if (captureMouse) {
                SetCursor(NULL);
            } else {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
            break;


        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return TRUE;
}

/**
 * AboutDldProc
 *
 */
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg)
    {
    case WM_INITDIALOG :
        return TRUE ;

    case WM_COMMAND :
        switch (LOWORD (wParam))
        {
        case IDOK :
        case IDCANCEL :
            EndDialog (hDlg, 0) ;
            return TRUE ;
        }
        break ;
    }
    return FALSE ;
}

/**
 * createWindow (IMPROVE rename, as it doesn't create a window)
 *
 * Called on program startup.
 *
 * @param x Width of video mode
 * @param y Height of video mode
 * @returns ALWAYS 0 (IMPROVE)
 */
int createWindow(ARMul_State *state, int x, int y)
{
   xSize = x;
   ySize = y;

   pbmi = calloc(1, sizeof(BITMAPINFOHEADER) + (sizeof(RGBQUAD) * 256));
   cbmi = calloc(1, sizeof(BITMAPINFOHEADER) + (sizeof(RGBQUAD) * 256));
   mbmi = calloc(1, sizeof(BITMAPINFOHEADER) + (sizeof(RGBQUAD) * 256));
   InitializeCriticalSection(&bmpCriticalSection);

   CreateThread(NULL, 16384, threadWindow, state, 0, &tid);

   return 0;
}

/**
 * createBitmaps
 *
 * Called when vidc recieves new display start and end parameters
 *
 * @param hWidth  New width in pixels
 * @param hHeight New height in pixels
 * @param hBpp    New bits per pixel
 * @param hXScale New X scale factor
 * @returns ALWAYS 0 (IMPROVE)
 */
int createBitmaps(int hWidth, int hHeight, int hBpp, int hXScale)
{
  size_t dibpitch, curpitch, mskpitch;

  EnterCriticalSection(&bmpCriticalSection);

  if (hbmp) DeleteObject(hbmp), hbmp = NULL;
  if (cbmp) DeleteObject(cbmp), cbmp = NULL;
  if (mbmp) DeleteObject(mbmp), mbmp = NULL;

  memset(pbmi, 0, sizeof(BITMAPINFOHEADER));
  memset(cbmi, 0, sizeof(BITMAPINFOHEADER));

  /* Setup display bitmap */
  pbmi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
  pbmi->bmiHeader.biWidth         = hWidth;
  pbmi->bmiHeader.biHeight        = -hHeight;
  pbmi->bmiHeader.biCompression   = BI_RGB; /* 0; */
  pbmi->bmiHeader.biPlanes        = 1;
  pbmi->bmiHeader.biSizeImage     = 0; /* wic->getWidth()*wic->getHeight()*scale; */
  pbmi->bmiHeader.biBitCount      = hBpp;
  pbmi->bmiHeader.biXPelsPerMeter = 0;
  pbmi->bmiHeader.biYPelsPerMeter = 0;
  pbmi->bmiHeader.biClrUsed       = 0;
  pbmi->bmiHeader.biClrImportant  = 0;
  hbmp = CreateDIBSection(NULL, pbmi, DIB_RGB_COLORS, &dibbmp, NULL, 0);
  dibpitch = (pbmi->bmiHeader.biWidth + 3) & ~3;
  dibstride = (dibpitch * pbmi->bmiHeader.biBitCount) >> 3;

  /* Setup Cursor bitmap */
  cbmi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
  cbmi->bmiHeader.biWidth         = 32*hXScale;
  cbmi->bmiHeader.biHeight        = -hHeight;
  cbmi->bmiHeader.biCompression   = BI_RGB; /* 0; */
  cbmi->bmiHeader.biPlanes        = 1;
  cbmi->bmiHeader.biSizeImage     = 0; /* wic->getWidth()*wic->getHeight()*scale; */
  cbmi->bmiHeader.biBitCount      = hBpp;
  cbmi->bmiHeader.biXPelsPerMeter = 0;
  cbmi->bmiHeader.biYPelsPerMeter = 0;
  cbmi->bmiHeader.biClrUsed       = 0;
  cbmi->bmiHeader.biClrImportant  = 0;
  cbmp = CreateDIBSection(NULL, cbmi, DIB_RGB_COLORS, &curbmp, NULL, 0);
  curpitch = (cbmi->bmiHeader.biWidth + 3) & ~3;
  curstride = (curpitch * cbmi->bmiHeader.biBitCount) >> 3;

  if (hBpp <= 8) {
    /* Setup Cursor mask */
    mbmi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
    mbmi->bmiHeader.biWidth         = 32*hXScale;
    mbmi->bmiHeader.biHeight        = -hHeight;
    mbmi->bmiHeader.biCompression   = BI_RGB; /* 0; */
    mbmi->bmiHeader.biPlanes        = 1;
    mbmi->bmiHeader.biSizeImage     = 0; /* wic->getWidth()*wic->getHeight()*scale; */
    mbmi->bmiHeader.biBitCount      = 1;
    mbmi->bmiHeader.biXPelsPerMeter = 0;
    mbmi->bmiHeader.biYPelsPerMeter = 0;
    mbmi->bmiHeader.biClrUsed       = 0;
    mbmi->bmiHeader.biClrImportant  = 0;
    mbmi->bmiColors[0].rgbBlue  = 0;
    mbmi->bmiColors[0].rgbGreen = 0;
    mbmi->bmiColors[0].rgbRed   = 0;
    mbmi->bmiColors[1].rgbBlue  = 255;
    mbmi->bmiColors[1].rgbGreen = 255;
    mbmi->bmiColors[1].rgbRed   = 255;
    mbmp = CreateDIBSection(NULL, mbmi, DIB_RGB_COLORS, &mskbmp, NULL, 0);
    mskpitch = (mbmi->bmiHeader.biWidth + 3) & ~3;
    mskstride = (mskpitch * mbmi->bmiHeader.biBitCount) >> 3;
  }

  LeaveCriticalSection(&bmpCriticalSection);

  return 0;
}

void setPaletteColour(int i, uint8_t r, uint8_t g, uint8_t b)
{
  pbmi->bmiColors[i].rgbBlue  = b;
  pbmi->bmiColors[i].rgbGreen = g;
  pbmi->bmiColors[i].rgbRed   = r;
}

void setCursorPaletteColour(int i, uint8_t r, uint8_t g, uint8_t b)
{
  cbmi->bmiColors[i].rgbBlue  = b;
  cbmi->bmiColors[i].rgbGreen = g;
  cbmi->bmiColors[i].rgbRed   = r;
}

/**
 * updateDisplay
 *
 *
 * @returns ALWAYS 0 (IMPROVE)
 */
int updateDisplay(void)
{
  InvalidateRect(mainWin, NULL, 0);
  UpdateWindow(mainWin);

  return 0;
}

/**
 * resizeWindow
 *
 * Called when vidc recieves new display start and end parameters
 *
 * @param hWidth  New width in pixels
 * @param hHeight New width in pixels
 * @returns ALWAYS 0 (IMPROVE)
 */
int resizeWindow(int hWidth, int hHeight)
{
  RECT r;

  if (hWidth <= 0 || hWidth > xSize) {
    return 0;
  }
  if (hHeight <= 0 || hHeight > ySize) {
    return 0;
  }

  if (!GetWindowRect(mainWin, &r)) {
    return 0;
  }

  r.right = r.left + hWidth;
  r.bottom = r.top + hHeight;
  AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE);

  SetWindowPos(mainWin, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOCOPYBITS);

  return 0;
}
