///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              09.13.2026
///
/// @file              NanoOsDirentTypes.h
///
/// @brief             ino_t, off_t, and struct dirent, shared between
///                    src/kernel/Filesystem.h and src/user/NanoOsApi.h.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_DIRENT_TYPES_H
#define NANO_OS_DIRENT_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

// ino_t and off_t are deliberately kept out of sys/types.h and given their
// own header: src/kernel/Filesystem.h needs them (for struct dirent below)
// but must not drag gid_t/pid_t/uid_t along with it -- those have real,
// differently-shaped counterparts in the POSIX simulator build's system
// libc that HAL code depends on (e.g. src/hal/HalPosix.c, for real
// fork/kill/setuid-style calls), and pulling sys/types.h into that file's
// translation unit would shadow them with these narrower, NanoOs-specific
// ones. ino_t/off_t have no such real counterpart in use anywhere in this
// codebase (nothing here makes a real syscall using either name), so it's
// safe to let ours win over a system libc's own -- guarded with whichever
// macro name that libc itself uses to guard its own typedef, so whichever
// definition is processed first in a given translation unit wins and the
// second is skipped rather than conflicting.  Two different guard
// conventions are checked because this header is compiled against two
// different real libcs depending on target: glibc (__ino_t_defined /
// __off_t_defined, used by the POSIX simulator build) and newlib
// (_INO_T_DECLARED / _OFF_T_DECLARED, used by the arm-none-eabi-gcc/
// avr-gcc/ez80 cross toolchains) -- e.g. Arduino.h on SAMD21 targets pulls
// in newlib's <sys/types.h> before this header is ever reached.
#if !defined(__ino_t_defined) && !defined(_INO_T_DECLARED)
typedef uint64_t ino_t;
#define __ino_t_defined
#define _INO_T_DECLARED
#endif
#if !defined(__off_t_defined) && !defined(_OFF_T_DECLARED)
typedef int32_t off_t;
#define __off_t_defined
#define _OFF_T_DECLARED
#endif

// d_type values for struct dirent below.
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

/// @struct dirent
///
/// @brief A single directory entry, as returned by readdir.
///
/// @details This lives here -- rather than directly in Filesystem.h or
/// NanoOsApi.h -- because it must have a single definition, and both of
/// those headers end up included in the same translation unit in the
/// freestanding filesystem driver builds (contiguous/overlay): two
/// independent bodies for the same struct tag, even textually identical
/// ones, would be a redefinition error.
///
/// This is filesystem-agnostic: it is not shaped around what FAT32 happens
/// to store, so that a future, more Unix-like filesystem can populate it
/// correctly too.
///
/// @param d_ino A filesystem-specific identifier for this entry, matching
///   st_ino for the same entry (see NanoOsStatTypes.h).  For FAT32, this is
///   derived from the on-disk location (LBA and byte offset) of the entry's
///   own directory entry, not from the cluster its data starts at: FAT32 has
///   no real inode table, so the entry's location -- where its metadata
///   actually lives -- is what stands in for one. 64 bits wide so that
///   derivation has room to encode both the LBA and a slot within it (see
///   fat32EntryLocationToIno) without practical risk of two entries
///   colliding.
/// @param d_off An opaque, monotonically increasing per-DIR sequence number.
///   Not currently usable with seekdir/telldir (neither is implemented);
///   present for structural completeness.
/// @param d_reclen The size, in bytes, of this dirent (the fixed-size header
///   plus d_name's actual length and NUL terminator).
/// @param d_type One of DT_UNKNOWN, DT_DIR, or DT_REG.
/// @param d_name The NUL-terminated entry name.  Declared as a one-byte array
///   because this is the struct's last member and is intentionally
///   variable-length: each dirent is allocated with exactly
///   (offsetof(struct dirent, d_name) + strlen(name) + 1) bytes rather than a
///   fixed worst-case buffer.  Written as a one-element array rather than a
///   C99 flexible array member ([]) so this still compiles under a C++
///   compiler.
struct dirent {
  ino_t          d_ino;
  off_t          d_off;
  unsigned short d_reclen;
  unsigned char  d_type;
  char           d_name[1];
};

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_DIRENT_TYPES_H
