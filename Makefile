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
# Default endianness of the *emulated* processor (LITTLEEND or BIGEND)
# Should stay as LITTLEEND in most cases
ENDIAN=LITTLEEND

# Sound support - currently experimental - to enable set to 'yes'
SOUND_SUPPORT=no

# Windowing System
ifeq ($(SYSTEM),)
SYSTEM=X
endif


WARN = -Wall -Wno-return-type -Wno-unknown-pragmas -Wshadow -Wundef \
   -Wpointer-arith -Wcast-align -Wstrict-prototypes \
   -Wmissing-prototypes -Wmissing-declarations -Wnested-externs \
   -Wcast-qual


# add -DHOST_BIGENDIAN for big endian hosts, e.g. Sun, SGI, HP

ifeq ($(PROFILE),yes)
CFLAGS = -O -g -pg -ftest-coverage -fprofile-arcs
else
CFLAGS = -O3 -funroll-loops -fexpensive-optimizations -ffast-math \
    -fomit-frame-pointer -frerun-cse-after-loop
endif

CFLAGS += \
    -D$(ENDIAN) $(CFL) -DNOOS -DNOFPE $(WARN) \
    -I$(SYSTEM) -Iarch -I.

prefix=/usr/local

INSTALL_DIR = $(prefix)/bin
INSTALL=cp


# Everything else should be ok as it is.

OBJS = armcopro.o armemu.o arminit.o \
	armsupp.o main.o dagstandalone.o armos.o \
		armrdi.o $(SYSTEM)/DispKbd.o arch/i2c.o arch/archio.o \
    arch/fdc1772.o $(SYSTEM)/ControlPane.o arch/hdc63463.o arch/ReadConfig.o \
    arch/keyboard.o arch/extnrom.o

SRCS = armcopro.c armemu.c arminit.c arch/armarc.c \
	armsupp.c main.c dagstandalone.c armos.c  \
	arm-support.s conditions.s rhs.s \
	armrdi.c $(SYSTEM)/DispKbd.c arch/i2c.c arch/archio.c \
	arch/fdc1772.c $(SYSTEM)/ControlPane.c arch/hdc63463.c \
	arch/ReadConfig.c arch/keyboard.c

INCS = armdefs.h armemu.h armfpe.h armopts.h armos.h \
	dbg_conf.h dbg_cp.h dbg_hif.h dbg_rdi.h $(SYSTEM)/KeyTable.h \
  arch/i2c.h arch/archio.h arch/fdc1772.h arch/ControlPane.h \
  arch/hdc63463.h arch/keyboard.h

TARGET=arcem

ifeq (${SYSTEM},riscos)
CFLAGS += -DSYSTEM_riscos -Iriscos-single
TARGET=!ArcEm/arcem
endif

ifeq (${SYSTEM},riscos-single)
DIRECT_DISPLAY=yes
CFLAGS += -I@ -DSYSTEM_riscos_single -Iriscos-single -mpoke-function-name
OBJS += arm-support.o rhs.o
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
LIBS += -lpthread
INCS += arch/sound.h
endif


TARED = *.c *.s *.h README COPYING Makefile \
        X riscos riscos-single arch

MODEL = arch/armarc

VER=1.0

all: $(TARGET)

install: all
	$(INSTALL) armul $(INSTALL_DIR)

$(TARGET): $(OBJS) $(MODEL).o
	$(CC) $(OBJS) $(LIBS) $(MODEL).o -o $@


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

armos.o: armos.c armdefs.h armos.h armfpe.h
	$(CC) $(CFLAGS) -c $*.c

armcopro.o: armcopro.c armdefs.h
	$(CC) $(CFLAGS) -c $*.c

armemu.o: armemu.c armdefs.h armemu.h armemuinstr.c armemudec.c
	$(CC) $(CFLAGS) -o armemu.o -c armemu.c

arm-support.o: arm-support.s instructions
	$(CC) -x assembler-with-cpp arm-support.s -c -I@

rhs.o: rhs.s
	$(CC) rhs.s -c

instructions: armsuppmov.s armsuppmovs.s armsuppmvn.s armsuppmvns.s

armsuppmov.s: conditions.s
	sed s#ARMINS#Mov# <conditions.s >armsuppmov.s

armsuppmovs.s: conditions.s
	sed s#ARMINS#Movs# <conditions.s >armsuppmovs.s

armsuppmvn.s: conditions.s
	sed s#ARMINS#Mvn# <conditions.s >armsuppmvn.s

armsuppmvns.s: conditions.s
	sed s#ARMINS#Mvns# <conditions.s >armsuppmvns.s

arminit.o: arminit.c armdefs.h armemu.h
	$(CC) $(CFLAGS) -c $*.c

armrdi.o: armrdi.c armdefs.h armemu.h armos.h dbg_cp.h dbg_conf.h dbg_rdi.h \
		dbg_hif.h
	$(CC) $(CFLAGS) -c $*.c

armsupp.o: armsupp.c armdefs.h armemu.h
	$(CC) $(CFLAGS) -c $*.c

dagstandalone.o: dagstandalone.c armdefs.h dbg_conf.h dbg_hif.h dbg_rdi.h
	$(CC) $(CFLAGS) -c $*.c

main.o: main.c armdefs.h dbg_rdi.h dbg_conf.h
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
