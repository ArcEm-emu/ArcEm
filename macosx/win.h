
/*****************************************************************************
 * 
 * Copyright (C) 2002 Michael Dales
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * Name   : win.h
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/


#if 0
extern int nVirtKey;
extern int lKeyData;
extern int nKeyStat;
extern int keyF;
#else

#define KB_BUFFER_SIZE 20
extern int nVirtKey[KB_BUFFER_SIZE];
extern int nKeyStat[KB_BUFFER_SIZE];
extern int nKBRead;
extern int nKBWrite;
extern int keyF;
#endif

extern int nMouseX;
extern int nMouseY;
extern int mouseMF;

extern int rMouseX;
extern int rMouseY;
extern int rMouseHeight;

#if 0
extern int nButton;
extern int buttF;
#else
extern int nButton[KB_BUFFER_SIZE];
extern int buttF;
extern int nMouseRead;
extern int nMouseWrite;
#endif

extern int createWindow(int w, int h);
extern int updateDisplay(int, int, int, int, int);
extern int resizeWindow(int hWidth, int hHeight);
