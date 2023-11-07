#    Makefile for ARMulator:  ARM6 Instruction Emulator.
#    Copyright (C) 1994 Advanced RISC Machines Ltd.
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# Hacked for Archimedes emulation by Dave Gilbert (armul@treblig.org)
# Modified for different systems by Peter Naulls <peter@chocky.org>

# These variables can be overridden

# Extension ROM support - currently stable on X, Windows and Mac OS X
EXTNROM_SUPPORT=yes

# Sound support - currently experimental - to enable set to 'yes'
SOUND_SUPPORT=no

# Enable this if sound support uses pthreads
SOUND_PTHREAD=yes

# HostFS support - currently experimental - to enable set to 'yes'
HOSTFS_SUPPORT=yes

# Endianess of the Host system, the default is little endian (x86 and
# ARM. If you run on a big endian system such as Sparc and some versions
# of MIPS set this flag
HOST_BIGENDIAN=no

# Which version of the manual to include in the RiscPkg archive
# development, arcem-1.50, etc.
MANUAL?=development

# Windowing System
ifeq ($(SYSTEM),)
SYSTEM=X
endif

CROSS=
CC=$(CROSS)gcc
LD=$(CC)
WINDRES=$(CROSS)windres
LDFLAGS=

WARN = -Wall -W -Wno-unused-parameter \
   -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes \
   -Wmissing-prototypes -Wmissing-declarations -Wnested-externs \
   -Wcast-qual -Wwrite-strings

ifeq ($(PROFILE),yes)
CFLAGS = -O -g -pg -ftest-coverage -fprofile-arcs
LIBS += -lgcov
else
ifeq ($(DEBUG),yes)
CFLAGS += -O0 -g
else
CFLAGS = -O3 -funroll-loops -ffast-math -fomit-frame-pointer
# These don't exist in Clang, and are enabled with -O2 when using GCC.
# -fexpensive-optimizations -frerun-cse-after-loop
endif
endif

CFLAGS += $(WARN)
CPPFLAGS += -Ilibs/inih

PKG_CONFIG = pkg-config

prefix=/usr/local

INSTALL_DIR = $(prefix)/bin
INSTALL=cp


# Everything else should be ok as it is.

OBJS = armcopro.o armemu.o arminit.o \
	armsupp.o dagstandalone.o eventq.o hostfs.o \
		$(SYSTEM)/DispKbd.o arch/i2c.o arch/archio.o \
    arch/fdc1772.o $(SYSTEM)/ControlPane.o arch/hdc63463.o \
    arch/keyboard.o $(SYSTEM)/filecalls.o arch/filecommon.o \
    arch/ArcemConfig.o arch/cp15.o arch/newsound.o arch/displaydev.o \
    arch/filero.o arch/fileunix.o arch/filewin.o arch/extnrom.o \
    libs/inih/ini.o

SRCS = armcopro.c armemu.c arminit.c arch/armarc.c \
	armsupp.c dagstandalone.c eventq.c hostfs.c \
	$(SYSTEM)/DispKbd.c arch/i2c.c arch/archio.c \
	arch/fdc1772.c $(SYSTEM)/ControlPane.c arch/hdc63463.c \
	arch/keyboard.c $(SYSTEM)/filecalls.c \
	arch/ArcemConfig.c arch/cp15.c arch/newsound.c \
	arch/displaydev.c arch/filecommon.c \
	arch/filero.c arch/fileunix.c arch/filewin.c arch/extnrom.c \
	libs/inih/ini.c

INCS = armcopro.h armdefs.h armemu.h $(SYSTEM)/KeyTable.h \
  arch/i2c.h arch/archio.h arch/fdc1772.h arch/ControlPane.h \
  arch/hdc63463.h arch/keyboard.h arch/ArcemConfig.h arch/cp15.h \
  libs/inih/ini.h

TARGET=arcem

ifeq (${SYSTEM},amiga)
HOST_BIGENDIAN=yes
HOSTFS_SUPPORT=yes
EXTNROM_SUPPORT=yes
SOUND_SUPPORT=yes
SOUND_PTHREAD=no
SRCS += amiga/wb.c amiga/arexx.c amiga/sound.c
OBJS += amiga/wb.o amiga/arexx.o amiga/sound.o
CPPFLAGS += -D__LARGE64_FILES -D__USE_INLINE__
CFLAGS += -mcrt=newlib
LDFLAGS += -mcrt=newlib
# The following two lines are for Altivec support via libfreevec
# Uncomment them if you are using a G4 or other PPC with Altivec
#CFLAGS += -maltivec -mabi=altivec
#LDFLAGS += -maltivec -mabi=altivec -lfreevec
# The following line is for onchipmem support, which should speed
# the emulator up but slows it down, so it is disabled by default
#CPPFLAGS += -DONCHIPMEM_SUPPORT
endif

ifeq (${SYSTEM},amigaos3)
CROSS=m68k-amigaos-
HOST_BIGENDIAN=yes
HOSTFS_SUPPORT=yes
EXTNROM_SUPPORT=yes
SOUND_SUPPORT=yes
SOUND_PTHREAD=no
SRCS += amiga/wb.c amiga/sound.c
OBJS += amiga/wb.o amiga/sound.o
CPPFLAGS += -D__amigaos3__
CFLAGS += -noixemul
LDFLAGS += -noixemul
endif

ifeq (${SYSTEM},riscos-single)
# HostFS
HOSTFS_SUPPORT=yes
# Sound
SOUND_SUPPORT=yes
SOUND_PTHREAD=no
OBJS += riscos-single/sound.o riscos-single/soundbuf.o
# General
EXTNROM_SUPPORT=yes
CPPFLAGS += -DSYSTEM_riscos_single -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -mtune=cortex-a8 -march=armv5te -mthrowback -mfpu=fpa
LDFLAGS += -static
# Disable stack limit checks. -ffixed-sl required to prevent sl being used as temp storage, breaking unixlib and any other code that does do stack checks
CPPFLAGS += -DUSE_FAKEMAIN
CFLAGS += -mno-apcs-stack-check -ffixed-sl
OBJS += riscos-single/realmain.o
# No function name poking for a bit extra speed
CFLAGS += -mno-poke-function-name
# Debug options
#CFLAGS += -save-temps -mpoke-function-name
# Profiling
#CPPFLAGS += -DPROFILE_ENABLED
#CFLAGS += -mpoke-function-name
#OBJS += riscos-single/prof.o
TARGET=!ArcEm/arcem
# Create RiscPkg archive directory structure
# Final step to turn it into a zip archive must be done manually, either on RISC OS or via GCCSDK's native-zip
riscpkg: $(TARGET)
	rm -rf ArcEm
	mkdir -p ArcEm/Apps/Misc
	cp -r riscos-single/RiscPkg ArcEm
	cp -r !ArcEm ArcEm/Apps/Misc
	rm ArcEm/Apps/Misc/!ArcEm/arcem
	elf2aif !ArcEm/arcem ArcEm/Apps/Misc/!ArcEm/arcem,ff8
	cp hexcmos ArcEm/Apps/Misc/!ArcEm/hexcmos
	cp -r docs ArcEm/Apps/Misc/!ArcEm
	mkdir -p ArcEm/Apps/Misc/!ArcEm/extnrom
	find support_modules -name *,ffa -exec cp '{}' ArcEm/Apps/Misc/!ArcEm/extnrom \;
	wget http://arcem.sf.net/manual/$(MANUAL).html -O ArcEm/Apps/Misc/!ArcEm/manual.html
	cp docs/COPYING ArcEm/Apps/Misc
	mkdir ArcEm/Apps/Misc/hostfs
endif

ifeq (${SYSTEM},nds)
CC=arm-none-eabi-gcc
AS=arm-none-eabi-as
LD=$(CC)
ARM9_ARCH = -mthumb -mthumb-interwork -march=armv5te -mtune=arm946e-s
CFLAGS += $(ARM9_ARCH) -ffunction-sections -fdata-sections -DSYSTEM_nds -DARM9 -DUSE_FAKEMAIN -DNO_OPEN64 -isystem $(DEVKITPRO)/libnds/include -Wno-cast-align -Wno-format
LDFLAGS += -specs=ds_arm9.specs -g $(ARM9_ARCH) -Wl,--gc-sections -L$(DEVKITPRO)/libnds/lib
LIBS += -lfilesystem -lfat -lnds9
OBJS += nds/main.o nds/img/bg.o nds/img/keys.o
ifneq ($(DEBUG),yes)
CFLAGS += -DNDEBUG
endif

TARGET = ArcEm.elf
all: ArcEm.nds

%.nds: %.elf %.arm7.elf nds/arc.bmp
	mkdir -p romfs/extnrom
	cp support_modules/*/*,ffa romfs/extnrom
	ndstool -c $@ -9 $< -7 $*.arm7.elf -b nds/arc.bmp "ArcEm;Archimedes Emulator;WIP" -d romfs

%.s %.h: %.png %.grit
	grit $< -fts -o$*
%.s %.h: %.bmp %.grit
	grit $< -fts -o$*

nds/ControlPane.o: nds/KeyTable.h nds/img/bg.h nds/img/keys.h

ARM7_ARCH = -mthumb -mthumb-interwork -march=armv4t -mtune=arm7tdmi
ARM7_CFLAGS = $(ARM7_ARCH) -Os -ffunction-sections -fdata-sections -DARM7 -isystem $(DEVKITPRO)/libnds/include
ARM7_LDFLAGS = -specs=ds_arm7.specs -g $(ARM7_ARCH) -Wl,--gc-sections -L$(DEVKITPRO)/libnds/lib
ARM7_LIBS = -lnds7

ArcEm.arm7.elf: nds/arm7/main.arm7.o
	$(LD) $(ARM7_LDFLAGS) $^ $(ARM7_LIBS) -o $@

%.arm7.o: %.c
	$(CC) $(ARM7_CFLAGS) -o $@ -c $<
endif

ifeq (${SYSTEM},SDL)
SDL_CONFIG=sdl2-config
CPPFLAGS += -DSYSTEM_SDL
ifneq ($(shell uname),Darwin)
CPPFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
endif
CFLAGS += $(shell $(SDL_CONFIG) --cflags)
LIBS += $(shell $(SDL_CONFIG) --libs)
OBJS += SDL/fb.o SDL/render.o SDL/sound.o
SOUND_SUPPORT=yes
SOUND_PTHREAD=no
endif

ifeq (${SYSTEM},X)
CPPFLAGS += -DSYSTEM_X
ifneq ($(shell uname),Darwin)
CPPFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
endif
ifneq ($(shell $(PKG_CONFIG) --exists x11 xext && echo x11 xext),)
CFLAGS += $(shell $(PKG_CONFIG) --cflags x11 xext)
LIBS += $(shell $(PKG_CONFIG) --libs x11 xext)
else
CFLAGS += -I/usr/X11R6/include
LIBS += -L/usr/X11R6/lib -lXext -lX11
endif
OBJS += X/true.o X/pseudo.o X/sound.o
#SOUND_SUPPORT = yes
endif

ifeq (${SYSTEM},win)
TARGET = ArcEm.exe
CPPFLAGS += -DSYSTEM_win
OBJS += win/gui.o win/win.o win/sound.o
LIBS += -luser32 -lgdi32 -lcomdlg32 -lwinmm
# Comment the following line to have a console window
LIBS += -mwindows
SOUND_SUPPORT = yes
SOUND_PTHREAD = no
endif

ifeq (${SOUND_SUPPORT},yes)
CPPFLAGS += -DSOUND_SUPPORT
INCS += arch/sound.h
ifeq (${SOUND_PTHREAD},yes)
LIBS += -lpthread
endif
endif

ifeq (${HOSTFS_SUPPORT},yes)
CPPFLAGS += -DHOSTFS_SUPPORT
endif

ifeq (${EXTNROM_SUPPORT},yes)
CPPFLAGS += -DEXTNROM_SUPPORT
endif

ifeq (${HOST_BIGENDIAN},yes)
CPPFLAGS += -DHOST_BIGENDIAN
endif


TARED = *.c *.s *.h README COPYING Makefile \
        X riscos riscos-single arch

MODEL = arch/armarc

VER=1.0

all: $(TARGET)

install: all
	$(INSTALL) $(TARGET) $(INSTALL_DIR)

$(TARGET): $(OBJS) $(MODEL).o
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) $(MODEL).o -o $@

clean:
	rm -f *.o arch/*.o $(SYSTEM)/*.o libs/*/*.o $(TARGET) core *.bb *.bbg *.da

distclean: clean
	rm -f *~

realclean: distclean
	rm -f *.tar *.tar.gz

depend:
	makedepend $(SRCS)

arcem.tar.gz:
	rm -rf arcem-$(VER)
	mkdir arcem-$(VER)
	cd arcem-$(VER) ; \
	for file in $(TARED) ; do \
	  cp -a ../$${file} . ; \
	done ; \
	find -name .gitignore -type f | xargs rm; \
	touch dummy.o ; find -type f | grep '\.o$$' | xargs rm -r 2>/dev/null
	tar cf arcem.tar arcem-$(VER)
	gzip arcem.tar
	mv arcem.tar.gz arcem-$(VER).tar.gz

# memory models

arch/armarc.o: armdefs.h arch/armarc.c arch/armarc.h \
               arch/fdc1772.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/armarc.o

# other objects

armcopro.o: armcopro.c armdefs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c

armemu.o: armemu.c armdefs.h armemu.h armemuinstr.c armemudec.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o armemu.o -c armemu.c

riscos-single/prof.o: riscos-single/prof.s
	$(CC) $(CPPFLAGS) -x assembler-with-cpp riscos-single/prof.s -c -o $@

riscos-single/soundbuf.o: riscos-single/soundbuf.s
	$(CC) $(CPPFLAGS) -x assembler-with-cpp riscos-single/soundbuf.s -c -o $@

riscos-single/realmain.o: riscos-single/realmain.s
	$(CC) $(CPPFLAGS) -x assembler-with-cpp riscos-single/realmain.s -c -o $@

arminit.o: arminit.c armdefs.h armemu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c

armsupp.o: armsupp.c armdefs.h armemu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c

dagstandalone.o: dagstandalone.c armdefs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c

eventq.o: eventq.c eventq.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c

$(SYSTEM)/DispKbd.o: $(SYSTEM)/DispKbd.c $(SYSTEM)/KeyTable.h \
                     arch/armarc.h arch/fdc1772.h arch/hdc63463.h \
                     arch/keyboard.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o $(SYSTEM)/DispKbd.o

arch/i2c.o: arch/i2c.c arch/i2c.h arch/armarc.h arch/archio.h \
            arch/fdc1772.h arch/hdc63463.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/i2c.o

arch/archio.o: arch/archio.c arch/archio.h arch/armarc.h arch/i2c.h \
        arch/fdc1772.h arch/hdc63463.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/archio.o

arch/fdc1772.o: arch/fdc1772.c arch/fdc1772.h arch/armarc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/fdc1772.o

arch/hdc63463.o: arch/hdc63463.c arch/hdc63463.h arch/armarc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/hdc63463.o

$(SYSTEM)/ControlPane.o: $(SYSTEM)/ControlPane.c arch/ControlPane.h \
        arch/armarc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o $(SYSTEM)/ControlPane.o

arch/keyboard.o: arch/keyboard.c arch/keyboard.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/keyboard.o

arch/newsound.o: arch/newsound.c arch/sound.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/newsound.o

arch/displaydev.o: arch/displaydev.c arch/displaydev.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o arch/displaydev.o

win/gui.o: win/gui.rc win/gui.h win/arc.ico
	$(WINDRES) $(CPPFLAGS) $*.rc -o win/gui.o

X/true.o: X/true.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o X/true.o

X/pseudo.o: X/pseudo.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o X/pseudo.o


# DO NOT DELETE
