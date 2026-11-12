#!/bin/sh
# Build dist/MonumentsOfMars-win64.zip with MSYS2's mingw64 toolchain, from the repository's
# root, in an MSYS2 MINGW64 shell or Git Bash:
#
#     packaging/windows/build-zip.sh
#
# Needs the MSYS2 packages mingw-w64-x86_64-gcc, -cmake, -ninja and -SDL2 (MSYS2 in C:\msys64,
# or MINGW_PREFIX naming another mingw64 folder). The zip holds one folder with mars.exe,
# SDL2.dll, the README and the licences; the game's files go beside mars.exe.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
mingw=${MINGW_PREFIX:-/c/msys64/mingw64}
PATH=$mingw/bin:$PATH
export PATH

build=$root/build/windows
stage=$build/package
name=MonumentsOfMars
zip=$root/dist/$name-win64.zip

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build"

rm -rf "$stage"
mkdir -p "$stage/$name"
out=$stage/$name
strip -o "$out/mars.exe" "$build/mars.exe"
cp "$mingw/bin/SDL2.dll" "$out/"
cp "$mingw/share/licenses/SDL2/LICENSE.txt" "$out/LICENSE-SDL2.txt"
cp "$root/README.md" "$root/LICENSE" "$root/LICENSE-GPL-2.0.txt" "$out/"

# Nothing may need a DLL that isn't in the zip or in Windows itself (a libgcc, a libwinpthread).
bad=$(objdump -p "$out/mars.exe" "$out/SDL2.dll" | sed -n 's/^\tDLL Name: //p' | sort -fu |
    grep -viE '^(sdl2|kernel32|user32|gdi32|shell32|advapi32|ole32|oleaut32|imm32|winmm|version|setupapi|cfgmgr32|msvcrt|ucrtbase|api-ms-win-.*)\.dll$' || true)
if [ -n "$bad" ]; then
    echo "mars.exe or SDL2.dll needs DLLs the zip doesn't have:" >&2
    echo "$bad" >&2
    exit 1
fi

# Windows' own tar writes zips (MSYS2's is GNU tar, which doesn't).
mkdir -p "$root/dist"
rm -f "$zip"
(cd "$stage" && "${SYSTEMROOT:-C:/Windows}/System32/tar.exe" -a -cf "$(cygpath -w "$zip")" "$name")
"${SYSTEMROOT:-C:/Windows}/System32/tar.exe" -tf "$(cygpath -w "$zip")"
echo "built dist/$name-win64.zip"
