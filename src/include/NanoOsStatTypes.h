///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              09.13.2026
///
/// @file              NanoOsStatTypes.h
///
/// @brief             mode_t and struct stat, shared between
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

#ifndef NANO_OS_STAT_TYPES_H
#define NANO_OS_STAT_TYPES_H

#include <stdint.h>
#include <stddef.h>

// ino_t and off_t come from NanoOsDirentTypes.h (already shared with struct
// dirent for the same reason struct stat lives here -- see that header for
// the full rationale).  uid_t and gid_t come from sys/types.h.  Neither of
// those two headers is touched here: sys/types.h in particular is included,
// unguarded, from specific points in the HAL (see src/hal/HalCommon.c) to
// deliberately win a race against a real libc's own uid_t/gid_t/pid_t in the
// POSIX simulator build; giving it the same per-typedef "whichever comes
// first wins" guard used below for the types that are new to this header
// could cause a *real* system header included later in the same translation
// unit to see its own guard already tripped and skip defining the distinct,
// real-syscall-facing type it was renamed to (see the uid_t/gid_t/pid_t
// rename dance in HalCommon.c). Simply reusing these two headers as they
// already exist -- relying on their own top-of-file include guards for
// idempotency -- avoids that hazard entirely.
#include "sys/types.h"
#include "NanoOsDirentTypes.h"
#include "time.h"

#ifdef __cplusplus
extern "C"
{
#endif

// mode_t, nlink_t, dev_t, blksize_t and blkcnt_t are new names -- nothing
// else in this codebase typedefs or renames them -- so, unlike uid_t/gid_t
// above, it's safe to give them the same defensive "yield to a real libc's
// own typedef, whichever is processed first in a given translation unit
// wins" treatment NanoOsDirentTypes.h uses for ino_t/off_t.  Two guard
// conventions are checked for the same reason as there: glibc
// (__foo_t_defined) and newlib (_FOO_T_DECLARED).
#if !defined(__mode_t_defined) && !defined(_MODE_T_DECLARED)
typedef uint32_t mode_t;
#define __mode_t_defined
#define _MODE_T_DECLARED
#endif
#if !defined(__nlink_t_defined) && !defined(_NLINK_T_DECLARED)
typedef uint16_t nlink_t;
#define __nlink_t_defined
#define _NLINK_T_DECLARED
#endif
#if !defined(__dev_t_defined) && !defined(_DEV_T_DECLARED)
typedef uint16_t dev_t;
#define __dev_t_defined
#define _DEV_T_DECLARED
#endif
#if !defined(__blksize_t_defined) && !defined(_BLKSIZE_T_DECLARED)
typedef int32_t blksize_t;
#define __blksize_t_defined
#define _BLKSIZE_T_DECLARED
#endif
#if !defined(__blkcnt_t_defined) && !defined(_BLKCNT_T_DECLARED)
typedef int32_t blkcnt_t;
#define __blkcnt_t_defined
#define _BLKCNT_T_DECLARED
#endif

// File-type and permission bits for st_mode, with the standard POSIX values
// (so anything that formats them the usual way, e.g. an "ls -l"-style
// listing, works without translation).
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

/// @def FILESYSTEM_UID_UNKNOWN
/// @def FILESYSTEM_GID_UNKNOWN
///
/// @brief Sentinel st_uid/st_gid values a filesystem driver writes into a
/// struct stat it has no concept of file ownership for (FAT32 being the
/// first example).  This is the entire contract between such a driver and
/// the driver-agnostic FILESYSTEM_LSTAT command handler (see
/// usr/src/filesystems/common/LstatCommandHandler.c): the handler recognizes
/// these two values and fills in real defaults (and a fully-open permission
/// mode) in their place, without ever knowing *why* a given driver couldn't
/// populate them itself.
#define FILESYSTEM_UID_UNKNOWN ((uid_t) -1)
#define FILESYSTEM_GID_UNKNOWN ((gid_t) -1)

/// @struct stat
///
/// @brief POSIX file status structure, as returned by lstat.
///
/// @details This is filesystem-agnostic, matching the standard POSIX layout
/// rather than anything FAT32 happens to track, so that a future, more
/// Unix-like filesystem can populate it fully.  A driver that has no notion
/// of a given field (ownership being the only such field FAT32 has) signals
/// that with the FILESYSTEM_UID_UNKNOWN/FILESYSTEM_GID_UNKNOWN sentinels
/// above rather than by omitting the field.
struct stat {
  dev_t     st_dev;
  ino_t     st_ino;
  mode_t    st_mode;
  nlink_t   st_nlink;
  uid_t     st_uid;
  gid_t     st_gid;
  dev_t     st_rdev;
  off_t     st_size;
  blksize_t st_blksize;
  blkcnt_t  st_blocks;
  time_t    st_atime;
  time_t    st_mtime;
  time_t    st_ctime;
};

/// @fn void filesystemFixupUnknownOwnership(struct stat *statbuf)
///
/// @brief Replace a driver's FILESYSTEM_UID_UNKNOWN/FILESYSTEM_GID_UNKNOWN
/// sentinels with real defaults.
///
/// @details Shared by the FILESYSTEM_LSTAT and FILESYSTEM_ISTAT command
/// handlers (see usr/src/filesystems/common/) so the two apply an
/// identical, driver-agnostic policy: this is the entire extent of what
/// either handler knows about *why* a driver couldn't populate ownership --
/// it never learns which filesystem produced the sentinel, only that it
/// did.
///
/// @note Deliberately touches st_uid/st_gid only, never st_mode's
/// permission bits: those are the driver's call, not this layer's.  A
/// filesystem that can't track ownership may still track something
/// permission-shaped (e.g. FAT32's read-only attribute), and this code has
/// no way to know whether it does; overwriting the driver's own bits here
/// would silently discard whatever it *did* know. The driver already
/// leaves st_uid/st_gid at these sentinels specifically to signal "I don't
/// track this" -- it sets st_mode's permission bits to whatever is actually
/// correct on its own, unconditionally, before this ever runs.
///
/// @note Deliberately defined here rather than in Filesystem.h, right next
/// to the two sentinels it compares against: those expand to "((uid_t) -1)"
/// and "((gid_t) -1)", and uid_t/gid_t are renamed to different, undefined
/// names around certain includes in some translation units this header
/// reaches (see e.g. HalCommon.c). This header is already reliably reached
/// -- for struct stat itself -- before any of those renames take effect;
/// Filesystem.h is not guaranteed to be.
///
/// @param statbuf A pointer to a struct stat a driver has already
///   populated, to fix up in place.
static inline void filesystemFixupUnknownOwnership(struct stat *statbuf) {
  if ((statbuf->st_uid == FILESYSTEM_UID_UNKNOWN)
    && (statbuf->st_gid == FILESYSTEM_GID_UNKNOWN)
  ) {
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
  }
}

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_STAT_TYPES_H
