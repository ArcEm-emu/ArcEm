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

#include <stdlib.h>

#include "dagstandalone.h"
#include "ArcemConfig.h"
#include "prof.h"

#ifdef WIN32

#include <windows.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
#ifdef DEBUG
  AllocConsole( );  

  freopen("CONIN$","rb",stdin);   
  freopen("CONOUT$","wb",stdout); 
  freopen("CONOUT$","wb",stderr); 
#endif

  // Setup the default values for the config system
  ArcemConfig_SetupDefaults();

  dagstandalone();

  return EXIT_SUCCESS;
}

#else

// Main function for X, RISC OS and MacOS X versions


#ifdef USE_FAKEMAIN
int fakemain(int argc,char *argv[]);

int fakemain(int argc,char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
  Prof_Init();
  
  // Setup the default values for the config system
  ArcemConfig_SetupDefaults();

  // Parse any commandline arguments given to the program
  // to overrule the defautls
  ArcemConfig_ParseCommandLine(argc, argv);

  dagstandalone();

  return EXIT_SUCCESS;
}

#endif



