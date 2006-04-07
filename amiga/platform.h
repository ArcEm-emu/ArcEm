#include <intuition/intuition.h>

struct XColor
{
	ULONG red;
	ULONG green;
	ULONG blue;
};

  struct {
    char *ImageData,*CursorImageData;

    /* Map from host memory contents to 'pixel' value for putpixel */
    unsigned long pixelMap[256];

    /* Map from host memory contents to 'pixel' value for putpixel in cursor*/
    unsigned long cursorPixelMap[4];
    int red_shift,red_prec;
    int green_shift,green_prec;
    int blue_shift,blue_prec;

	struct Window *window;

  } HostDisplay;
