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

# Windowing System
ifeq ($(SYSTEM),)
SYSTEM=riscos-single
endif

CC=gcc
LD=gcc
LDFLAGS=

# Armulator core endianess of the *emulated* processor (LITTLEEND or BIGEND)
# Should stay as LITTLEEND when used in ArcEm
ENDIAN=LITTLEEND

WARN = -Wall -Wno-return-type -Wno-unknown-pragmas -Wshadow \
   -Wpointer-arith -Wcast-align -Wstrict-prototypes \
   -Wmissing-prototypes -Wmissing-declarations -Wnested-externs \
   -Wcast-qual -Wwrite-strings

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
    -D$(ENDIAN) $(CFL) $(WARN) \
    -I$(SYSTEM) -Iarch -I.

prefix=/usr/local

INSTALL_DIR = $(prefix)/bin
INSTALL=cp


# Everything else should be ok as it is.

OBJS = armcopro.o armemu.o arminit.o \
	armsupp.o main.o dagstandalone.o eventq.o \
		$(SYSTEM)/DispKbd.o arch/i2c.o arch/archio.o \
    arch/fdc1772.o $(SYSTEM)/ControlPane.o arch/hdc63463.o arch/ReadConfig.o \
    arch/keyboard.o $(SYSTEM)/filecalls.o arch/DispKbdShared.o \
    arch/ArcemConfig.o arch/cp15.o

SRCS = armcopro.c armemu.c arminit.c arch/armarc.c \
	armsupp.c main.c dagstandalone.c eventq.c \
	$(SYSTEM)/DispKbd.c arch/i2c.c arch/archio.c \
	arch/fdc1772.c $(SYSTEM)/ControlPane.c arch/hdc63463.c \
	arch/ReadConfig.c arch/keyboard.c $(SYSTEM)/filecalls.c \
	arch/DispKbdShared.c arch/ArcemConfig.c arch/cp15.c

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
CFLAGS += -mcrt=newlib
LDFLAGS += -mcrt=newlib
# The following two lines are for Altivec support via libfreevec
# Uncomment them if you are using a G4 or other PPC with Altivec
#CFLAGS += -maltivec -mabi=altivec
#LDFLAGS += -maltivec -mabi=altivec -lfreevec
endif

ifeq (${SYSTEM},gp2x)
CC=arm-linux-gcc
LD=$(CC)
DIRECT_DISPLAY=yes
SYSROOT = D:/gp2x/devkitGP2X/sysroot
CFLAGS += -DSYSTEM_gp2x -Igp2x -I$(SYSROOT)/usr/include
LIBS += -L$(SYSROOT)/usr/lib -static
LD=$(CC)
TARGET=arcem.gpe
endif

ifeq (${SYSTEM},riscos)
EXTNROM_SUPPORT=notyet
CFLAGS += -DSYSTEM_riscos -Iriscos-single
TARGET=!ArcEm/arcem
endif

ifeq (${SYSTEM},riscos-single)
EXTNROM_SUPPORT=yes
HOSTFS_SUPPORT=yes
DIRECT_DISPLAY=yes
CFLAGS += -I@ -DSYSTEM_riscos_single -Iriscos-single -mtune=xscale -march=armv5te -mthrowback
LDFLAGS += -static
# Disable stack limit checks. -ffixed-sl required to prevent sl being used as temp storage, breaking unixlib and any other code that does do stack checks
#CFLAGS += -mno-apcs-stack-check -ffixed-sl
# No function name poking for a bit extra speed
CFLAGS += -mno-poke-function-name
# Debug options
#CFLAGS += -save-temps -mpoke-function-name
# Profiling
#CFLAGS += -mpoke-function-name -DPROFILE_ENABLED
OBJS += prof.o
HOSTFS_OBJS = riscos-single/hostfs.o
TARGET=!ArcEm/arcem
endif

ifeq (${SYSTEM},X)
CFLAGS += -DSYSTEM_X -I/usr/X11R6/include
LIBS += -L/usr/X11R6/lib -lXext -lX11
endif

ifeq (${SYSTEM},win)
CFLAGS += -DSYSTEM_win -mno-cygwin
OBJS += win/gui.o win/win.o
LIBS += -luser32 -lgdi32 -mno-cygwin
# Comment the following line to have a console window
LIBS += -mwindows
endif

ifeq (${DIRECT_DISPLAY},yes)
CFLAGS += -DDIRECT_DISPLAY
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
        X riscos riscos-single gp2x arch

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
	find -type d | grep CVS | xargs rm -r; \
	touch dummy.o ; find -type f | grep '\.o$$' | xargs rm -r 2>/dev/null
	tar cf arcem.tar arcem-$(VER)
	gzip arcem.tar
	mv arcem.tar.gz arcem-$(VER).tar.gz

# memory models

arch/armarc.o: armdefs.h arch/armarc.c arch/DispKbd.h arch/armarc.h \
               arch/fdc1772.h
	$(CC) $(CFLAGS) -c $*.c -o arch/armarc.o

# other objects

armcopro.o: armcopro.c armdefs.h
	$(CC) $(CFLAGS) -c $*.c

armemu.o: armemu.c armdefs.h armemu.h armemuinstr.c armemudec.c
	$(CC) $(CFLAGS) -o armemu.o -c armemu.c

prof.o: prof.s
	$(CC) -x assembler-with-cpp prof.s -c

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

$(SYSTEM)/DispKbd.o: $(SYSTEM)/DispKbd.c arch/DispKbd.h $(SYSTEM)/KeyTable.h \
                     arch/armarc.h arch/fdc1772.h arch/hdc63463.h \
                     arch/keyboard.h
	$(CC) $(CFLAGS) -c $*.c -o $(SYSTEM)/DispKbd.o

arch/i2c.o: arch/i2c.c arch/i2c.h arch/armarc.h arch/DispKbd.h arch/archio.h \
            arch/fdc1772.h arch/hdc63463.h
	$(CC) $(CFLAGS) -c $*.c -o arch/i2c.o

arch/archio.o: arch/archio.c arch/archio.h arch/armarc.h arch/i2c.h \
        arch/DispKbd.h arch/fdc1772.h arch/hdc63463.h
	$(CC) $(CFLAGS) -c $*.c -o arch/archio.o

arch/fdc1772.o: arch/fdc1772.c arch/fdc1772.h arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o arch/fdc1772.o

arch/hdc63463.o: arch/hdc63463.c arch/hdc63463.h arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o arch/hdc63463.o

$(SYSTEM)/ControlPane.o: $(SYSTEM)/ControlPane.c arch/ControlPane.h \
        arch/DispKbd.h arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o $(SYSTEM)/ControlPane.o

arch/ReadConfig.o: arch/ReadConfig.c arch/ReadConfig.h arch/DispKbd.h \
	arch/armarc.h
	$(CC) $(CFLAGS) -c $*.c -o arch/ReadConfig.o

arch/keyboard.o: arch/keyboard.c arch/keyboard.h
	$(CC) $(CFLAGS) -c $*.c -o arch/keyboard.o

win/gui.o: win/gui.rc win/gui.h win/arc.ico
	windres $*.rc -o win/gui.o


# DO NOT DELETE
