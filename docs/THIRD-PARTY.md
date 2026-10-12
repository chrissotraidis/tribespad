# Third-party software

TribesPad builds and links the following open-source projects. Each is fetched at the
pinned revision in [dependencies.lock.json](../dependencies.lock.json) on the player's
own Mac; this repository does not vendor their sources. Their license texts are
copied into the app where the build bundles them (`LICENSE.ANGLE`,
`LICENSE.gl4es`) and are in each upstream source tree.

## In the app

| Project | Version | License | Use |
|---|---|---|---|
| [SDL](https://github.com/libsdl-org/SDL) | 3.2.18 | zlib | Windows, events, audio devices |
| [GL4ES](https://github.com/ptitSeb/gl4es) | ec16bed | MIT | The game's OpenGL 1.x on OpenGL ES |
| [ANGLE](https://chromium.googlesource.com/angle/angle) (prebuilt by [build-angle](https://github.com/jeremyfa/build-angle)) | d2e5e48 | BSD-3-Clause | OpenGL ES on Metal |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | dc7d705 | LGPL-2.0-or-later | Audio mixing |
| [Bochs](https://github.com/bochs-emu/Bochs) FPU and SoftFloat-3e files only | 93a047c | LGPL-2.1 | Exact x87 transcendental functions |
| [Berkeley SoftFloat](http://www.jhauser.us/arithmetic/SoftFloat.html) | 3e | BSD-3-Clause | Exact 80-bit floating point |
| [zlib-ng](https://github.com/zlib-ng/zlib-ng) | 2.2.3 | zlib | Compression used by TribesNEXT |
| [curl](https://curl.se) | 8.22.0 | curl | HTTP for TribesNEXT services and server lists |
| [LibreSSL](https://www.libressl.org) | 4.3.2 | ISC / OpenSSL / SSLeay | TLS for curl |

OpenAL Soft and the Bochs files are under the GNU Lesser/Library GPL and are linked
statically. Every player builds the app from the corresponding sources with the scripts
in this repository and keeps the object files in `generated/`, so the library
can be modified and the app relinked.

## Build tools only (not in the app)

| Project | License | Use |
|---|---|---|
| [SR](https://github.com/M-HT/SR) (SRW and llasm), with [udis86](https://github.com/vmt/udis86) | MIT; udis86 BSD-2-Clause | Static recompiler for the x86 binaries |
| [LDC](https://github.com/ldc-developers/ldc) 1.42.0 | BSD-3-Clause and Boost | Compiles llasm |
| [REWise](https://codeberg.org/CYBERDEV/REWise) | GPL-3.0 | Extracts the official Wise installer |
| [HaloPad / ProjectReach](https://github.com/chrissotraidis/projectreach) helpers | same owner | SRW macOS patch, executable audit, llasm support |
| LLVM 20, CMake, Ninja, 7-Zip, zstd (Homebrew) | various | Compilers and build tools |

Tribes 2 and TribesNEXT are not part of TribesPad; the build downloads them from their
official sources. Tribes 2 is owned by its rights holders, and TribesNEXT by its authors.
