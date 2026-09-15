///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              09.14.2026
///
/// @file              NanoOsGrp.h
///
/// @brief             NanoOs implementation of grp.h functionality.
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


#ifndef NANO_OS_USER_GRP_H
#define NANO_OS_USER_GRP_H

#include "NanoOsSysTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def NANO_OS_MAX_GROUPNAME_LENGTH
///
/// @brief The maximum number of characters that a group name can be.
#define NANO_OS_MAX_GROUPNAME_LENGTH 16

/// @def NANO_OS_GRP_STRING_BUF_SIZE
///
/// @brief The size to use for the character buffer that holds the strings in
/// the call to getgrnam_r.
#define NANO_OS_GRP_STRING_BUF_SIZE 96

struct group {
  char   *gr_name;        /* group name */
  char   *gr_passwd;      /* group password */
  gid_t   gr_gid;         /* group ID */
  char  **gr_mem;         /* NULL-terminated array of pointers
                             to names of group members */
};

int nanoOsGetgrnam_r(
  const char *name,
  struct group *grp,
  char *buf,
  size_t buflen,
  struct group **result);
int nanoOsGetgrgid_r(
  gid_t gid,
  struct group *grp,
  char *buf,
  size_t buflen,
  struct group **result);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_GRP_H

