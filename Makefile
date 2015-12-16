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

CC=gcc
LD=gcc
LDFLAGS=

WARN = -Wall -Wno-return-type -Wno-unknown-pragmas -Wshadow \
   -Wpointer-arith -Wcast-align -Wstrict-prototypes \
   -Wmissing-prototypes -Wmissing-declarations -Wnested-externs \
   -Wcast-qual -Wwrite-strings -Wno-unused

ifeq ($(PROFILE),yes)
CFLAGS = -O -g -pg -ftest-coverage -fprofile-arcs
LIBS += -lgcov
else
ifeq ($(DEBUG),yes)
CFLAGS += -O0 -g
else
CFLAGS = -O3 -funroll-loops -fexpensive-optimizations -ffast-math \
    -fomit-frame-pointer -frerun-cse-after-loop
endif
endif

CFLAGS += \
    $(CFL) $(WARN) \
    -I$(SYSTEM) -Iarch -I.

prefix=/usr/local

INSTALL_DIR = $(prefix)/bin
INSTALL=cp


# Everything else should be ok as it is.

OBJS = armcopro.o armemu.o arminit.o \
	armsupp.o main.o dagstandalone.o eventq.o \
		$(SYSTEM)/DispKbd.o arch/i2c.o arch/archio.o \
    arch/fdc1772.o $(SYSTEM)/ControlPane.o arch/hdc63463.o arch/ReadConfig.o \
    arch/keyboard.o $(SYSTEM)/filecalls.o arch/filecommon.o \
    arch/ArcemConfig.o arch/cp15.o arch/newsound.o arch/displaydev.o

SRCS = armcopro.c armemu.c arminit.c arch/armarc.c \
	armsupp.c main.c dagstandalone.c eventq.c \
	$(SYSTEM)/DispKbd.c arch/i2c.c arch/archio.c \
	arch/fdc1772.c $(SYSTEM)/ControlPane.c arch/hdc63463.c \
	arch/ReadConfig.c arch/keyboard.c $(SYSTEM)/filecalls.c \
	arch/ArcemConfig.c arch/cp15.c arch/newsound.c \
	arch/displaydev.c arch/filecommon.c

INCS = armdefs.h armemu.h $(SYSTEM)/KeyTable.h \
  arch/i2c.h arch/archio.h arch/fdc1772.h arch/ControlPane.h \
  arch/hdc63463.h arch/keyboard.h arch/ArcemConfig.h arch/cp15.h

TARGET=arcem

ifeq (${SYSTEM},amiga)
HOST_BIGENDIAN=yes
HOSTFS_SUPPORT=yes
EXTNROM_SUPPORT=yes
SOUND_SUPPORT=yes
SOUND_PTHREAD=no
SRCS += amiga/wb.c amiga/arexx.c
OBJS += amiga/wb.o amiga/arexx.o
CFLAGS += -mcrt=newlib -D__LARGE64_FILES -D__USE_INLINE__
LDFLAGS += -mcrt=newlib
# The following two lines are for Altivec support via libfreevec
# Uncomment them if you are using a G4 or other PPC with Altivec
#CFLAGS += -maltivec -mabi=altivec
#LDFLAGS += -maltivec -mabi=altivec -lfreevec
endif

ifeq (${SYSTEM},amigaos3)
CC=m68k-amigaos-gcc
LD=$(CC)
HOST_BIGENDIAN=yes
HOSTFS_SUPPORT=yes
EXTNROM_SUPPORT=yes
SOUND_SUPPORT=yes
SOUND_PTHREAD=no
SRCS += amiga/wb.c
OBJS += amiga/wb.o
CFLAGS += -D__amigaos3__ -noixemul
LDFLAGS += -noixemul
endif

ifeq (${SYSTEM},riscos-single)
# HostFS
HOSTFS_SUPPORT=yes
# Sound
SOUND_SUPPORT=yes
SOUND_PTHREAD=no
OBJS += riscos-single/soundbuf.o
# General
EXTNROM_SUPPORT=yes
CFLAGS += -I@ -DSYSTEM_riscos_single -Iriscos-single -mtune=cortex-a8 -march=armv5te -mthrowback -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -mfpu=fpa
LDFLAGS += -static
# Disable stack limit checks. -ffixed-sl required to prevent sl being used as temp storage, breaking unixlib and any other code that does do stack checks
CFLAGS += -mno-apcs-stack-check -ffixed-sl -DUSE_FAKEMAIN
OBJS += riscos-single/realmain.o
# No function name poking for a bit extra speed
CFLAGS += -mno-poke-function-name
# Debug options
#CFLAGS += -save-temps -mpoke-function-name
# Profiling
#CFLAGS += -mpoke-function-name -DPROFILE_ENABLED
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
	cp arcemrc ArcEm/Apps/Misc/!ArcEm/.arcemrc
	cp hexcmos ArcEm/Apps/Misc/!ArcEm/hexcmos
	cp -r docs ArcEm/Apps/Misc/!ArcEm
	mkdir -p ArcEm/Apps/Misc/!ArcEm/extnrom
	find support_modules -name *,ffa -exec cp '{}' ArcEm/Apps/Misc/!ArcEm/extnrom \;
	find ArcEm -name CVS -type d | xargs rm -r
	find ArcEm -name .cvsignore -type f | xargs rm
	wget http://arcem.sf.net/manual/$(MANUAL).html -O ArcEm/Apps/Misc/!ArcEm/manual.html
	cp docs/COPYING ArcEm/Apps/Misc
	mkdir ArcEm/Apps/Misc/hostfs
endif

ifeq (${SYSTEM},X)
CFLAGS += -DSYSTEM_X -I/usr/X11R6/include
ifneq ($(shell uname),Darwin)
CFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
endif
LIBS += -L/usr/X11R6/lib -lXext -lX11
OBJS += X/true.o X/pseudo.o
#SOUND_SUPPORT = yes
endif

ifeq (${SYSTEM},win)
CC = gcc-3
LD = gcc-3
CFLAGS += -DSYSTEM_win -mno-cygwin
OBJS += win/gui.o win/win.o
LIBS += -luser32 -lgdi32 -mno-cygwin
# Comment the following line to have a console window
LIBS += -mwindows
endif

ifeq (${SOUND_SUPPORT},yes)
CFLAGS += -DSOUND_SUPPORT
OBJS += $(SYSTEM)/sound.o
INCS += arch/sound.h
ifeq (${SOUND_PTHREAD},yes)
LIBS += -lpthread
endif
endif

ifeq (${HOSTFS_SUPPORT},yes)
CFLAGS += -DHOSTFS_SUPPORT
HOSTFS_OBJS ?= hostfs.o
OBJS += ${HOSTFS_OBJS}
endif

ifeq (${EXTNROM_SUPPORT},yes)
CFLAGS += -DEXTNROM_SUPPORT
OBJS += arch/extnrom.o
endif

ifeq (${HOST_BIGENDIAN},yes)
CFLAGS += -DHOST_BIGENDIAN
endif


TARED = *.c *.s *.h README COPYING Makefile \
        X riscos riscos-single arch

MODEL = arch/armarc

VER=1.0

all: $(TARGET)

install: all
	$(INSTALL) $(TARGET) $(INSTALL_DIR)
	f=arcemrc; test -f $$HOME/.$$f || $(INSTALL) $$f $(HOME)/.$$f

$(TARGET): $(OBJS) $(MODEL).o
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) $(MODEL).o -o $@

clean:
	rm -f *.o arch/*.o $(SYSTEM)/*.o arcem core *.bb *.bbg *.da

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
	find -name CVS -type d | xargs rm -r; \
	find -name .cvsignore -type f | xargs rm; \
	touch dummy.o ; find -type f | grep '\.o$$' | xargs rm -r 2>/dev/null
	tar cf arcem.tar arcem-$(VER)
	gzip arcem.tar
	mv arcem.tar.gz arcem-$(VER).tar.gz

# memory models

arch/armarc.o: armdefs.h arch/armarc.c arch/armarc.h \
               arch/fdc1772.h
	$(CC) $(CFLAGS) -c $*.c -o arch/armarc.o

# other objects

armcopro.o: armcopro.c armdefs.h
	$(CC) $(CFLAGS) -c $*.c

armemu.o: armemu.c armdefs.h armemu.h armemuinstr.c armemudec.c
	$(CC) $(CFLAGS) -o armemu.o -c armemu.c

riscos-single/prof.o: riscos-single/prof.s
	$(CC) -x assembler-with-cpp riscos-single/prof.s -c -o $@

riscos-single/soundbuf.o: riscos-single/soundbuf.s
	$(CC) -x assembler-with-cpp riscos-single/soundbuf.s -c -o $@

riscos-single/realmain.o: riscos-single/realmain.s
	$(CC) -x assembler-with-cpp riscos-single/realmain.s -c -o $@

arminit.o: arminit.c armdefs.h armemu.h
	$(CC) $(CFLAGS) -c $*.c

armsupp.o: armsupp.c armdefs.h armemu.h
	$(CC) $(CFLAGS) -c $*.c

dagstandalone.o: dagstandalone.c armdefs.h
	$(CC) $(CFLAGS) -c $*.c

main.o: main.c armdefs.h
	$(CC) $(CFLAGS) -c $*.c

eventq.o: eventq.c eventq.h
	$(CC) $(CFLAGS) -c $*.c

$(SYSTEM)/DispKbd.o: $(SYSTEM)/DispKbd.c $(SYSTEM)/KeyTable.h \
                     arch/armarc.h arch/fdc1772.h arch/hdc63463.h \
                     arch/keyboard.h
	$(CC) $(CFLAGS) -c $*.c -o $(SYSTEM)/DispKbd.o

arch/i2c.o: arch/i2c.c arch/i2c.h arch/armarc.h arch/archio.h \
            arch/fdc1772.h arch/hdc63463.h
	$(CC) $(CFLAGS) -c $*.c -o arch/i2c.o

arch/archio.o: arch/archio.c arch/archio.h arch/armarc.h arch/i2c.h \
        arch/fdc1772.h arch/hdc63463.h
	$(CC) $(CFLAGS) -c $*.c -o arch/archio.o

arch/fdc1772.o: arch/fdc1772.c arch/fdc1772.h arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o arch/fdc1772.o

arch/hdc63463.o: arch/hdc63463.c arch/hdc63463.h arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o arch/hdc63463.o

$(SYSTEM)/ControlPane.o: $(SYSTEM)/ControlPane.c arch/ControlPane.h \
        arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o $(SYSTEM)/ControlPane.o

arch/ReadConfig.o: arch/ReadConfig.c arch/ReadConfig.h \
	arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o arch/ReadConfig.o

arch/keyboard.o: arch/keyboard.c arch/keyboard.h
	$(CC) $(CFLAGS) -c $*.c -o arch/keyboard.o

arch/newsound.o: arch/newsound.c arch/sound.h
	$(CC) $(CFLAGS) -c $*.c -o arch/newsound.o

arch/displaydev.o: arch/displaydev.c arch/displaydev.h
	$(CC) $(CFLAGS) -c $*.c -o arch/displaydev.o

win/gui.o: win/gui.rc win/gui.h win/arc.ico
	windres $*.rc -o win/gui.o

X/true.o: X/true.c
	$(CC) $(CFLAGS) -c $*.c -o X/true.o

X/pseudo.o: X/pseudo.c
	$(CC) $(CFLAGS) -c $*.c -o X/pseudo.o


# DO NOT DELETE
