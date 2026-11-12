cmake_minimum_required(VERSION 3.21)
project(mars VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED CONFIG)

add_executable(mars
    src/main.c
    src/platform.c
    src/gamepad.c
    src/cga.c
    src/gfx.c
    src/text.c
    src/font8x8.c
    src/pcx.c
    src/lzexe.c
    src/gamedata.c
    src/level.c
    src/game.c
    src/keyconfig.c
    src/bind.c
    src/controls.c
    src/screens.c
    src/hiscore.c
    src/menu.c
    src/options.c
    src/effects.c
    src/settings.c
    src/smooth.c
    src/crt.c
    src/console.c
    src/files.c
)

# Windows: a GUI program (no console window when double-clicked; see src/console.h), with
# an icon and version information (src/mars.rc).
if(WIN32)
    enable_language(RC)
    set_target_properties(mars PROPERTIES WIN32_EXECUTABLE TRUE)
    configure_file(src/version.h.in version.h @ONLY)
    target_sources(mars PRIVATE src/mars.rc)
    target_include_directories(mars PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    # the icon's path in mars.rc is from the repository's root
    set_property(SOURCE src/mars.rc APPEND PROPERTY INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(SOURCE src/mars.rc APPEND PROPERTY OBJECT_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/monuments-of-mars.ico)
endif()

if(TARGET SDL2::SDL2main)
    target_link_libraries(mars PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(mars PRIVATE SDL2::SDL2)
if(NOT WIN32)
    target_link_libraries(mars PRIVATE m)
endif()

# The build for the Miyoo Mini (Plus) under OnionUI (packaging/miyoo/build-onion-port.sh)
option(MARS_MIYOO "Build for the Miyoo Mini (Plus) handheld" OFF)
if(MARS_MIYOO)
    target_compile_definitions(mars PRIVATE MARS_MIYOO)
endif()

if(MSVC)
    target_compile_options(mars PRIVATE /W4)
else()
    target_compile_options(mars PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(mars PRIVATE -static-libgcc)
    endif()
endif()

# Put SDL2.dll next to the executable so it runs outside the build shell.
if(WIN32)
    add_custom_command(TARGET mars POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:mars> $<TARGET_FILE_DIR:mars>
        COMMAND_EXPAND_LISTS)
endif()

# `cmake --install` (the Linux AppImage's AppDir; packaging/linux/build-appimage.sh)
include(GNUInstallDirs)
install(TARGETS mars RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
