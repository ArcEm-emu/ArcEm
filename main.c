/*  main.c -- top level of ARMulator:  ARM6 Instruction Emulator.
    Copyright (C) 1994 Advanced RISC Machines Ltd.
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "dagstandalone.h"

#ifdef WIN32

#include <windows.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
  
#ifdef DEBUG
  AllocConsole( );  

  freopen("CONIN$","rb",stdin);   
  freopen("CONOUT$","wb",stdout); 
  freopen("CONOUT$","wb",stderr); 
#endif
	
  dagstandalone();
  return 0;
}

#else

int main(int argc, char *argv[]) {
  dagstandalone();
  return 0;
}

#endif


