cmake_minimum_required(VERSION 3.21)
project(mars C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED CONFIG)

add_executable(mars
    src/main.c
    src/platform.c
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
    src/screens.c
    src/hiscore.c
    src/menu.c
)

if(TARGET SDL2::SDL2main)
    target_link_libraries(mars PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(mars PRIVATE SDL2::SDL2)

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
