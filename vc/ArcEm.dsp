# Microsoft Developer Studio Project File - Name="ArcEm" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=ArcEm - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "ArcEm.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "ArcEm.mak" CFG="ArcEm - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "ArcEm - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "ArcEm - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "ArcEm - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /W3 /GX /O2 /I "../arch" /I "../win" /I ".." /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "SYSTEM_win" /D "NOOS" /D "NOFPE" /D "EXTNROM_SUPPORT" /FR /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x809 /d "NDEBUG"
# ADD RSC /l 0x809 /i ".." /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /machine:I386 /out:"../ArcEm.exe"

!ELSEIF  "$(CFG)" == "ArcEm - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /I "../arch/" /I "../win/" /I ".." /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "SYSTEM_win" /D "NOOS" /D "NOFPE" /D "EXTNROM_SUPPORT" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x809 /d "_DEBUG"
# ADD RSC /l 0x809 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /debug /machine:I386 /out:"ArcEm.exe" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "ArcEm - Win32 Release"
# Name "ArcEm - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "win"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\win\arc.ico
# End Source File
# Begin Source File

SOURCE=..\win\ControlPane.c
# End Source File
# Begin Source File

SOURCE=..\win\DispKbd.c
# End Source File
# Begin Source File

SOURCE=..\win\filecalls.c
# End Source File
# Begin Source File

SOURCE=..\win\filecalls_internal.h
# End Source File
# Begin Source File

SOURCE=..\win\gui.h
# End Source File
# Begin Source File

SOURCE=..\win\gui.rc
# End Source File
# Begin Source File

SOURCE=..\win\KeyTable.h
# End Source File
# Begin Source File

SOURCE=..\win\win.c
# End Source File
# Begin Source File

SOURCE=..\win\win.h
# End Source File
# End Group
# Begin Group "arch"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\arch\ArcemConfig.c
# End Source File
# Begin Source File

SOURCE=..\arch\ArcemConfig.h
# End Source File
# Begin Source File

SOURCE=..\arch\archio.c
# End Source File
# Begin Source File

SOURCE=..\arch\archio.h
# End Source File
# Begin Source File

SOURCE=..\arch\armarc.c
# End Source File
# Begin Source File

SOURCE=..\arch\armarc.h
# End Source File
# Begin Source File

SOURCE=..\arch\ControlPane.h
# End Source File
# Begin Source File

SOURCE=..\arch\cp15.c
# End Source File
# Begin Source File

SOURCE=..\arch\cp15.h
# End Source File
# Begin Source File

SOURCE=..\arch\DispKbd.h
# End Source File
# Begin Source File

SOURCE=..\arch\DispKbdShared.c
# End Source File
# Begin Source File

SOURCE=..\arch\extnrom.c
# End Source File
# Begin Source File

SOURCE=..\arch\extnrom.h
# End Source File
# Begin Source File

SOURCE=..\arch\fdc1772.c
# End Source File
# Begin Source File

SOURCE=..\arch\fdc1772.h
# End Source File
# Begin Source File

SOURCE=..\arch\filecalls.h
# End Source File
# Begin Source File

SOURCE=..\arch\hdc63463.c
# End Source File
# Begin Source File

SOURCE=..\arch\hdc63463.h
# End Source File
# Begin Source File

SOURCE=..\arch\i2c.c
# End Source File
# Begin Source File

SOURCE=..\arch\i2c.h
# End Source File
# Begin Source File

SOURCE=..\arch\keyboard.c
# End Source File
# Begin Source File

SOURCE=..\arch\ReadConfig.c
# End Source File
# Begin Source File

SOURCE=..\arch\ReadConfig.h
# End Source File
# Begin Source File

SOURCE=..\arch\Version.h
# End Source File
# End Group
# Begin Source File

SOURCE=..\armcopro.c
# End Source File
# Begin Source File

SOURCE=..\armdefs.h
# End Source File
# Begin Source File

SOURCE=..\armemu.c
# End Source File
# Begin Source File

SOURCE=..\armemu.h
# End Source File
# Begin Source File

SOURCE=..\armfpe.h
# End Source File
# Begin Source File

SOURCE=..\arminit.c
# End Source File
# Begin Source File

SOURCE=..\armopts.h
# End Source File
# Begin Source File

SOURCE=..\armos.c
# End Source File
# Begin Source File

SOURCE=..\armos.h
# End Source File
# Begin Source File

SOURCE=..\armrdi.c
# End Source File
# Begin Source File

SOURCE=..\armrdi.h
# End Source File
# Begin Source File

SOURCE=..\armsupp.c
# End Source File
# Begin Source File

SOURCE=..\dagstandalone.c
# End Source File
# Begin Source File

SOURCE=..\dagstandalone.h
# End Source File
# Begin Source File

SOURCE=..\dbg_conf.h
# End Source File
# Begin Source File

SOURCE=..\dbg_cp.h
# End Source File
# Begin Source File

SOURCE=..\dbg_hif.h
# End Source File
# Begin Source File

SOURCE=..\dbg_rdi.h
# End Source File
# Begin Source File

SOURCE=..\main.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
