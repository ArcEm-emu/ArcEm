/* platform.h structures and functions specific to the X platform 

  Peter Howkins 21/12/2004
*/
#ifndef _PLATFORM_H_
#define _PLATFORM_H_


struct host_display {
  Window RootWindow,BackingWindow,MainPane,ControlPane,CursorPane;
  Display *disp;
  Screen *xScreen;
  int ScreenNum;
  XVisualInfo visInfo;
  XImage *DisplayImage,*CursorImage;
  char *ImageData,*CursorImageData;
  Colormap DefaultColormap;
  Colormap ArcsColormap;
  GC MainPaneGC;
  GC ControlPaneGC;
  XColor White,Black,Red,Green,GreyDark,GreyLight,OffWhite;
  XFontStruct *ButtonFont;

  /* Stuff for shape masking of the pointer based on XEyes */
  Pixmap shape_mask; /* window shape */
  int ShapeEnabled;      /* Yep - we are using shapes */
  char *ShapePixmapData; /* This is what we use to create the pixmap */
  unsigned long pixelMap[256]; /* Map from host memory contents to 'pixel' value for putpixel */
  unsigned long border_map;
  unsigned long cursorPixelMap[4]; /* Map from host memory contents to 'pixel' value for putpixel in cursor*/
  int red_shift,red_prec;
  int green_shift,green_prec;
  int blue_shift,blue_prec;
//  } HostDisplay;
};

extern struct host_display HD;


#endif /* _PLATFORM_H_ */
