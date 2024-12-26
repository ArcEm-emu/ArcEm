cmake_minimum_required(VERSION 3.0.2...3.10)
if(POLICY CMP0074)
	cmake_policy(SET CMP0074 NEW)
endif()
project(ArcEm LANGUAGES C VERSION "1.50.2")

set(ARCEM_SOURCES
	armcopro.c
	armcopro.h
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
	hostfs.c
	hostfs.h
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
	arch/extnrom.c
	arch/extnrom.h
	arch/fdc1772.c
	arch/fdc1772.h
	arch/filecalls.h
	arch/filecommon.c
	arch/filero.c
	arch/fileunix.c
	arch/filewin.c
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
	SDL/sound.c
)
set(ARCEM_X_SOURCES
	X/ControlPane.c
	X/DispKbd.c
	X/filecalls.c
	X/KeyTable.h
	X/platform.h
	X/pseudo.c
	X/sound.c
	X/true.c
)
set(ARCEM_MACOSX_SOURCES
	macosx/ArcemController.h
	macosx/ArcemController.m
	macosx/ArcemEmulator.h
	macosx/ArcemEmulator.m
	macosx/ArcemView.h
	macosx/ArcemView.m
	macosx/ControlPane.m
	macosx/DispKbd.c
	macosx/KeyTable.h
	macosx/PreferenceController.h
	macosx/PreferenceController.m
	macosx/filecalls.m
	macosx/main.m
	macosx/win.h
)
set(ARCEM_MACOSX_RESOURCES
	macosx/Credits.rtf
	macosx/en.lproj/MainMenu.xib
	macosx/en.lproj/Preferences.xib
)
set(ARCEM_NDS_SOURCES
	nds/ControlPane.c
	nds/ControlPane.h
	nds/DispKbd.c
	nds/filecalls.c
	nds/KeyTable.h
	nds/main.c
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
	win/sound.c
	win/win.c
	win/win.h
)
set(ARCEM_EXTNROM_MODULES
	support_modules/hostfs/hostfs,ffa
	support_modules/hostfs/hostfsfiler,ffa
	support_modules/modes/ArcemModes,ffa
	support_modules/scrollwheel/scrollwheel,ffa
	support_modules/support/support,ffa
)

if(WIN32)
	set(DEFAULT_SYSTEM "win")
elseif(APPLE)
	set(DEFAULT_SYSTEM "macosx")
elseif(NINTENDO_DS)
	set(DEFAULT_SYSTEM "nds")
elseif(UNIX AND NOT HAIKU)
	set(DEFAULT_SYSTEM "X")
else()
	set(DEFAULT_SYSTEM "SDL2")
endif()
set(SYSTEM ${DEFAULT_SYSTEM} CACHE STRING "System to compile for. Options: X SDL3 SDL2 SDL1 macosx nds win")
set_property(CACHE SYSTEM PROPERTY STRINGS X SDL3 SDL2 SDL1 macosx nds win)

if(${SYSTEM} MATCHES "^(SDL[123])$")
	add_executable(arcem WIN32 ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_SDL_SOURCES} ${ARCEM_EXTNROM_MODULES})
	target_compile_definitions(arcem PRIVATE SYSTEM_SDL)

	if(${SYSTEM} STREQUAL "SDL3")
		find_package(SDL3 REQUIRED)
		target_include_directories(arcem PRIVATE ${SDL3_INCLUDE_DIRS})
		target_link_libraries(arcem PRIVATE ${SDL3_LIBRARIES})
		target_compile_definitions(arcem PRIVATE SYSTEM_SDL3)
	elseif(${SYSTEM} STREQUAL "SDL2")
		find_package(SDL2 REQUIRED)
		target_include_directories(arcem PRIVATE ${SDL2_INCLUDE_DIRS})
		target_link_libraries(arcem PRIVATE ${SDL2_LIBRARIES})
		target_compile_definitions(arcem PRIVATE SYSTEM_SDL2)
	elseif(${SYSTEM} STREQUAL "SDL1")
		find_package(SDL REQUIRED)
		target_include_directories(arcem PRIVATE ${SDL_INCLUDE_DIR})
		target_link_libraries(arcem PRIVATE ${SDL_LIBRARY})
		target_compile_definitions(arcem PRIVATE SYSTEM_SDL1)
	endif()

	option(SOUND_SUPPORT "Build with sound support" ON)
	if(SOUND_SUPPORT)
		target_compile_definitions(arcem PRIVATE SOUND_SUPPORT)
	endif()
elseif(${SYSTEM} STREQUAL "X")
	add_executable(arcem ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_X_SOURCES} ${ARCEM_EXTNROM_MODULES})
	target_compile_definitions(arcem PRIVATE SYSTEM_X)

	find_package(X11 REQUIRED)
	target_include_directories(arcem PRIVATE ${X11_INCLUDE_DIR})
	target_link_libraries(arcem PRIVATE ${X11_LIBRARIES})

	option(SOUND_SUPPORT "Build with sound support" OFF)
	option(SOUND_PTHREAD "Build with pthreads for sound support" ON)
	if(SOUND_SUPPORT)
		target_compile_definitions(arcem PRIVATE SOUND_SUPPORT)

		if(SOUND_PTHREAD)
			find_package(Threads REQUIRED)
			target_link_libraries(Threads::Threads)
		endif()
	endif()
elseif(${SYSTEM} STREQUAL "macosx")
	enable_language(OBJC)

	add_executable(arcem ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_MACOSX_SOURCES} ${ARCEM_MACOSX_RESOURCES} ${ARCEM_EXTNROM_MODULES})
	target_compile_definitions(arcem PRIVATE SYSTEM_macosx)

	set_target_properties(arcem PROPERTIES
		LINKER_LANGUAGE OBJC
		RESOURCE "${ARCEM_MACOSX_RESOURCES}"
	)
	find_library(FRAMEWORKS Cocoa Foundation AppKit REQUIRED)
	target_link_libraries(arcem PRIVATE ${FRAMEWORKS})

	set_property(TARGET arcem APPEND_STRING PROPERTY COMPILE_FLAGS "-fobjc-arc")
elseif(${SYSTEM} STREQUAL "nds")
	add_executable(arcem ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_NDS_SOURCES} ${ARCEM_EXTNROM_MODULES})
	target_compile_definitions(arcem PRIVATE SYSTEM_nds USE_FAKEMAIN NO_OPEN64)

	target_compile_options(arcem PRIVATE -mthumb -mthumb-interwork)
	target_link_options(arcem PRIVATE -Wl,--gc-sections -mthumb -mthumb-interwork)

	grit_add_binary_target(bg nds/img/bg.png)
	grit_add_binary_target(font nds/img/font.png NO_MAP)
	grit_add_binary_target(keys nds/img/keys.png NO_MAP)
	dkp_add_embedded_binary_library(grit_data bg font keys)
	target_link_libraries(arcem PRIVATE grit_data filesystem fat)
elseif(${SYSTEM} STREQUAL "win")
	add_executable(arcem WIN32 ${ARCEM_SOURCES} ${ARCEM_ARCH_SOURCES} ${ARCEM_WIN_SOURCES} ${ARCEM_EXTNROM_MODULES})
	target_compile_definitions(arcem PRIVATE SYSTEM_win)

	target_link_libraries(arcem PRIVATE comdlg32 gdi32 winmm)

	option(SOUND_SUPPORT "Build with sound support" ON)
	if(SOUND_SUPPORT)
		target_compile_definitions(arcem PRIVATE SOUND_SUPPORT)
	endif()
else()
	message(FATAL_ERROR "Invalid system specified: ${SYSTEM}")
endif()

option(USE_SYSTEM_INIH "Use external inih library, rather than bundled copy" OFF)
if(USE_SYSTEM_INIH)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(INIH REQUIRED inih)
	target_include_directories(arcem PRIVATE ${INIH_INCLUDE_DIRS})
	target_link_libraries(arcem PRIVATE ${INIH_LINK_LIBRARIES})
else()
	target_include_directories(arcem PRIVATE "libs/inih")
	add_library(arcem-inih STATIC ${ARCEM_INIH_SOURCES})
	target_link_libraries(arcem PRIVATE arcem-inih)
endif()

if(WIN32)
	set_target_properties(arcem PROPERTIES OUTPUT_NAME "ArcEm")
	install(TARGETS arcem DESTINATION .)
	install(FILES ${ARCEM_EXTNROM_MODULES} DESTINATION extnrom)
	set(CPACK_GENERATOR "ZIP")
	include(CPack)
elseif(APPLE)
	set_source_files_properties(${ARCEM_EXTNROM_MODULES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/extnrom")
	set_target_properties(arcem PROPERTIES
		OUTPUT_NAME "ArcEm"
		RESOURCES "${ARCEM_EXTNROM_MODULES}"
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_BUNDLE_NAME ${CMAKE_PROJECT_NAME}
		MACOSX_BUNDLE_BUNDLE_VERSION ${CMAKE_PROJECT_VERSION}
	)

	install(TARGETS arcem
		BUNDLE DESTINATION . COMPONENT Runtime
		RUNTIME DESTINATION bin COMPONENT Runtime
	)
	if(${SYSTEM} MATCHES "^(SDL[123])$")
		install(CODE "include(BundleUtilities)
		fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/ArcEm.app\" \"\" \"\")")
	endif()

	set(CPACK_GENERATOR "DragNDrop")
	include(CPack)
elseif(NINTENDO_DS)
	file(COPY ${ARCEM_EXTNROM_MODULES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/nitrofs/extnrom)
	set_target_properties(arcem PROPERTIES OUTPUT_NAME "ArcEm")
	nds_create_rom(arcem
		SUBTITLE1 "Archimedes Emulator"
		SUBTITLE2 "WIP"
		ICON nds/arc.bmp
		NITROFS ${CMAKE_CURRENT_BINARY_DIR}/nitrofs
	)
endif()

option(EXTNROM_SUPPORT "Build with Extension ROM support" ON)
if(EXTNROM_SUPPORT)
	target_compile_definitions(arcem PRIVATE EXTNROM_SUPPORT)
endif()

option(HOSTFS_SUPPORT "Build with HostFS support" ON)
if(HOSTFS_SUPPORT)
	target_compile_definitions(arcem PRIVATE HOSTFS_SUPPORT)
endif()

include(TestBigEndian)
test_big_endian(HOST_BIGENDIAN)
if(HOST_BIGENDIAN)
	target_compile_definitions(arcem PRIVATE HOST_BIGENDIAN)
endif(HOST_BIGENDIAN)

if(MSVC)
	target_compile_definitions(arcem PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
	target_sources(arcem PRIVATE ${ARCEM_VC_SOURCES})
else()
	if(NOT APPLE)
		target_compile_definitions(arcem PRIVATE _LARGEFILE_SOURCE _LARGEFILE64_SOURCE _FILE_OFFSET_BITS=64)
	endif()

	target_compile_options(arcem PRIVATE -Wall -W -Wno-unused-parameter
		   -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes
		   -Wmissing-prototypes -Wmissing-declarations -Wnested-externs
		   -Wcast-qual -Wwrite-strings)
	target_compile_options(arcem PRIVATE $<$<CONFIG:Release>:-funroll-loops -ffast-math -fomit-frame-pointer>)
	# These don't exist in Clang, and are enabled with -O2 when using GCC.
	# -fexpensive-optimizations -frerun-cse-after-loop)
endif()

source_group(src FILES ${ARCEM_SOURCES})
source_group(src\\arch FILES ${ARCEM_ARCH_SOURCES})
source_group(src\\X FILES ${ARCEM_X_SOURCES})
source_group(src\\SDL FILES ${ARCEM_SDL_SOURCES})
source_group(src\\macosx FILES ${ARCEM_MACOSX_SOURCES})
source_group(src\\macosx FILES ${ARCEM_MACOSX_RESOURCES})
source_group(src\\nds FILES ${ARCEM_NDS_SOURCES})
source_group(src\\vc FILES ${ARCEM_VC_SOURCES})
source_group(src\\win FILES ${ARCEM_WIN_SOURCES})
source_group(libs\\inih FILES ${ARCEM_INIH_SOURCES})
source_group(extnrom FILES ${ARCEM_EXTNROM_MODULES})
