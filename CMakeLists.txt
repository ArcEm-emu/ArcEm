cmake_minimum_required(VERSION 3.0.2...3.10)
if(POLICY CMP0074)
	cmake_policy(SET CMP0074 NEW)
endif()
project(ArcEm LANGUAGES C)

set(ARCEM_SOURCES
	armcopro.c
	armdefs.h
	armemu.c
	armemu.h
	arminit.c
	armsupp.c
	c99.h
	dagstandalone.c
	dagstandalone.h
	eventq.c
	eventq.h
	main.c
	prof.h
)
set(ARCEM_ARCH_SOURCES
	arch/ArcemConfig.c
	arch/ArcemConfig.h
	arch/archio.c
	arch/archio.h
	arch/armarc.c
	arch/armarc.h
	arch/ControlPane.h
	arch/cp15.c
	arch/cp15.h
	arch/dbugsys.h
	arch/displaydev.c
	arch/displaydev.h
	arch/fdc1772.c
	arch/fdc1772.h
	arch/filecalls.h
	arch/filecommon.c
	arch/filero.c
	arch/filero.h
	arch/fileunix.c
	arch/fileunix.h
	arch/filewin.c
	arch/filewin.h
	arch/hdc63463.c
	arch/hdc63463.h
	arch/i2c.c
	arch/i2c.h
	arch/keyboard.c
	arch/keyboard.h
	arch/newsound.c
	arch/sound.h
	arch/Version.h
)
set(ARCEM_INIH_SOURCES
	libs/inih/ini.c
	libs/inih/ini.h
)
set(ARCEM_SDL_SOURCES
	SDL/ControlPane.c
	SDL/DispKbd.c
	SDL/filecalls.c
	SDL/fb.c
	SDL/KeyTable.h
	SDL/render.c
)
set(ARCEM_X_SOURCES
	X/ControlPane.c
	X/DispKbd.c
	X/filecalls.c
	X/KeyTable.h
	X/platform.h
	X/pseudo.c
	X/true.c
)
set(ARCEM_VC_SOURCES
	vc/dirent.h
)
set(ARCEM_WIN_SOURCES
	win/ControlPane.c
	win/DispKbd.c
	win/filecalls.c
	win/gui.h
	win/gui.rc
	win/KeyTable.h
	win/win.c
	win/win.h
)

if(WIN32)
	set(DEFAULT_SYSTEM "win")
elseif(UNIX AND NOT APPLE AND NOT HAIKU)
	set(DEFAULT_SYSTEM "X")
else()
	set(DEFAULT_SYSTEM "SDL2")
endif()
set(SYSTEM ${DEFAULT_SYSTEM} CACHE STRING "System to compile for. Options: X SDL2 SDL1 win")
set_property(CACHE SYSTEM PROPERTY STRINGS X SDL2 SDL1 win)

option(EXTNROM_SUPPORT "Build with Extension ROM support" ON)
if(EXTNROM_SUPPORT)
	add_definitions(-DEXTNROM_SUPPORT)
	list(APPEND ARCEM_ARCH_SOURCES arch/extnrom.c arch/extnrom.h)
endif()

option(HOSTFS_SUPPORT "Build with HostFS support" ON)
if(HOSTFS_SUPPORT)
	add_definitions(-DHOSTFS_SUPPORT)
	list(APPEND ARCEM_SOURCES hostfs.c hostfs.h)
endif()

include(TestBigEndian)
test_big_endian(HOST_BIGENDIAN)
if(HOST_BIGENDIAN)
	add_definitions(-DHOST_BIGENDIAN)
endif(HOST_BIGENDIAN)

if(${SYSTEM} MATCHES "^(SDL[12])$")
	add_executable(arcem WIN32 ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_INIH_SOURCES} ${ARCEM_SDL_SOURCES})
	target_include_directories(arcem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SDL)
	target_compile_definitions(arcem PRIVATE SYSTEM_SDL USE_FAKEMAIN)

	if(${SYSTEM} STREQUAL "SDL2")
		find_package(SDL2 REQUIRED)
		target_include_directories(arcem PRIVATE ${SDL2_INCLUDE_DIRS})
		target_link_libraries(arcem PRIVATE ${SDL2_LIBRARIES})
	elseif(${SYSTEM} STREQUAL "SDL1")
		find_package(SDL REQUIRED)
		target_include_directories(arcem PRIVATE ${SDL_INCLUDE_DIR})
		target_link_libraries(arcem PRIVATE ${SDL_LIBRARY})
	endif()
elseif(${SYSTEM} STREQUAL "X")
	option(SOUND_SUPPORT "Build with sound support" OFF)
	option(SOUND_PTHREAD "Build with pthreads for sound support" ON)
	if(SOUND_SUPPORT)
		add_definitions(-DSOUND_SUPPORT)
		list(APPEND ARCEM_X_SOURCES X/sound.c)
	endif()

	add_executable(arcem ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_INIH_SOURCES} ${ARCEM_X_SOURCES})
	target_include_directories(arcem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/X)
	target_compile_definitions(arcem PRIVATE SYSTEM_X)

	find_package(X11 REQUIRED)
	target_include_directories(arcem PRIVATE ${X11_INCLUDE_DIR})
	target_link_libraries(arcem PRIVATE ${X11_LIBRARIES})

	if(SOUND_SUPPORT AND SOUND_PTHREAD)
		find_package(Threads REQUIRED)
		target_link_libraries(Threads::Threads)
	endif()
elseif(${SYSTEM} STREQUAL "win")
	option(SOUND_SUPPORT "Build with sound support" ON)
	if(SOUND_SUPPORT)
		add_definitions(-DSOUND_SUPPORT)
		list(APPEND ARCEM_WIN_SOURCES win/sound.c)
	endif()

	add_executable(arcem WIN32 ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_INIH_SOURCES} ${ARCEM_WIN_SOURCES})
	target_include_directories(arcem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/win)
	target_compile_definitions(arcem PRIVATE SYSTEM_win)

	target_link_libraries(arcem PRIVATE comdlg32 gdi32 winmm)
else()
	message(FATAL_ERROR "Invalid system specified: ${SYSTEM}")
endif()

if(WIN32)
	set_target_properties(arcem PROPERTIES OUTPUT_NAME "ArcEm")
elseif(APPLE)
	set_target_properties(arcem PROPERTIES
		OUTPUT_NAME "ArcEm"
		MACOSX_BUNDLE TRUE
	)
endif()

target_include_directories(arcem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/arch)
if(MSVC)
	target_compile_definitions(arcem PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
	target_sources(arcem PRIVATE ${ARCEM_VC_SOURCES})
else()
	if(NOT APPLE)
		target_compile_definitions(arcem PRIVATE _LARGEFILE_SOURCE _LARGEFILE64_SOURCE _FILE_OFFSET_BITS=64)
	endif()

	target_compile_options(arcem PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
		   -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes
		   -Wmissing-prototypes -Wmissing-declarations -Wnested-externs
		   -Wcast-qual -Wwrite-strings)
	# TODO: Only add these for release builds
	target_compile_options(arcem PRIVATE -funroll-loops -ffast-math -fomit-frame-pointer)
	# These don't exist in Clang, and are enabled with -O2 when using GCC.
	# -fexpensive-optimizations -frerun-cse-after-loop)
endif()

source_group(src FILES ${ARCEM_SOURCES})
source_group(src\\arch FILES ${ARCEM_ARCH_SOURCES})
source_group(src\\X FILES ${ARCEM_X_SOURCES})
source_group(src\\SDL FILES ${ARCEM_SDL_SOURCES})
source_group(src\\vc FILES ${ARCEM_VC_SOURCES})
source_group(src\\win FILES ${ARCEM_WIN_SOURCES})
source_group(libs\\inih FILES ${ARCEM_INIH_SOURCES})
