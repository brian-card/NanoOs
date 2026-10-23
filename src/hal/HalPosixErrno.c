////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file HalPosixErrno.c
///
/// @brief Translation of the host's errno values into NanoOs's for the POSIX
/// simulator's HAL.

#ifdef __x86_64__

#include "user/NanoOsErrno.h"

/// @enum NanoOsErrnoValue
///
/// @brief NanoOs's errno values, captured before the host's errno.h redefines
/// the macros that name them.
enum NanoOsErrnoValue {
  NANO_OS_EOTHER       = EOTHER,
  NANO_OS_EBUSY        = EBUSY,
  NANO_OS_ENOMEM       = ENOMEM,
  NANO_OS_EACCES       = EACCES,
  NANO_OS_EINVAL       = EINVAL,
  NANO_OS_EIO          = EIO,
  NANO_OS_ENOSPC       = ENOSPC,
  NANO_OS_ENOENT       = ENOENT,
  NANO_OS_ENOTEMPTY    = ENOTEMPTY,
  NANO_OS_EOVERFLOW    = EOVERFLOW,
  NANO_OS_EFAULT       = EFAULT,
  NANO_OS_ENAMETOOLONG = ENAMETOOLONG,
  NANO_OS_EBADF        = EBADF,
  NANO_OS_ENODEV       = ENODEV,
  NANO_OS_ENOTTY       = ENOTTY,
  NANO_OS_ERANGE       = ERANGE,
  NANO_OS_ELOOP        = ELOOP,
  NANO_OS_ETIMEDOUT    = ETIMEDOUT,
  NANO_OS_ENOEXEC      = ENOEXEC,
  NANO_OS_ENOTSUP      = ENOTSUP,
  NANO_OS_ENXIO        = ENXIO,
  NANO_OS_EPERM        = EPERM,
  NANO_OS_ESRCH        = ESRCH,
  NANO_OS_EAGAIN       = EAGAIN,
  NANO_OS_ENOTDIR      = ENOTDIR,
  NANO_OS_EEXIST       = EEXIST,
};

#undef errno
#undef ENOERR
#undef EOTHER
#undef EBUSY
#undef ENOMEM
#undef EACCES
#undef EINVAL
#undef EIO
#undef ENOSPC
#undef ENOENT
#undef ENOTEMPTY
#undef EOVERFLOW
#undef EFAULT
#undef ENAMETOOLONG
#undef EBADF
#undef ENODEV
#undef ENOTTY
#undef ERANGE
#undef ELOOP
#undef ETIMEDOUT
#undef ENOEXEC
#undef ENOTSUP
#undef ENXIO
#undef EPERM
#undef ESRCH
#undef EAGAIN
#undef ENOTDIR
#undef EEXIST
#undef ELAST

#include <errno.h>

#include "HalPosix.h"

/// @fn int posixErrnoToNanoOs(int linuxErrno)
///
/// @brief Map a Linux errno value to the NanoOs errno value with the same
/// meaning.  Values NanoOs has no equivalent for map to EOTHER, and ENOSYS
/// maps to ENOTSUP.
///
/// @param linuxErrno A positive Linux errno value, or 0.
///
/// @return Returns the positive NanoOs errno value, or 0 if linuxErrno is 0.
int posixErrnoToNanoOs(int linuxErrno) {
  switch (linuxErrno) {
    case 0:            return 0;
    case EBUSY:        return NANO_OS_EBUSY;
    case ENOMEM:       return NANO_OS_ENOMEM;
    case EACCES:       return NANO_OS_EACCES;
    case EINVAL:       return NANO_OS_EINVAL;
    case EIO:          return NANO_OS_EIO;
    case ENOSPC:       return NANO_OS_ENOSPC;
    case ENOENT:       return NANO_OS_ENOENT;
    case ENOTEMPTY:    return NANO_OS_ENOTEMPTY;
    case EOVERFLOW:    return NANO_OS_EOVERFLOW;
    case EFAULT:       return NANO_OS_EFAULT;
    case ENAMETOOLONG: return NANO_OS_ENAMETOOLONG;
    case EBADF:        return NANO_OS_EBADF;
    case ENODEV:       return NANO_OS_ENODEV;
    case ENOTTY:       return NANO_OS_ENOTTY;
    case ERANGE:       return NANO_OS_ERANGE;
    case ELOOP:        return NANO_OS_ELOOP;
    case ETIMEDOUT:    return NANO_OS_ETIMEDOUT;
    case ENOEXEC:      return NANO_OS_ENOEXEC;
    case ENOTSUP:      return NANO_OS_ENOTSUP;
    case ENOSYS:       return NANO_OS_ENOTSUP;
    case ENXIO:        return NANO_OS_ENXIO;
    case EPERM:        return NANO_OS_EPERM;
    case ESRCH:        return NANO_OS_ESRCH;
    case EAGAIN:       return NANO_OS_EAGAIN;
    case ENOTDIR:      return NANO_OS_ENOTDIR;
    case EEXIST:       return NANO_OS_EEXIST;
    default:           return NANO_OS_EOTHER;
  }
}

#endif // __x86_64__
