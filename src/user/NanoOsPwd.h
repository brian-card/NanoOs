///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              01.25.2026
///
/// @file              NanoOsPwd.h
///
/// @brief             Functionality from pwd.h to be exported to user programs.
///
/// @copyright
///                   Copyright (c) 2012-2026 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_USER_PWD_H
#define NANO_OS_USER_PWD_H

#include "NanoOsSysTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def NANO_OS_MAX_PASSWORD_LENGTH
///
/// @brief The maximum number of characters that a user password can be.
#define NANO_OS_MAX_PASSWORD_LENGTH 32

/// @def NANO_OS_PASSWD_STRING_BUF_SIZE
///
/// @brief The size to use for the character buffer that holds the strings in
/// the call to getpwnam_r.
#define NANO_OS_PASSWD_STRING_BUF_SIZE 96

struct passwd {
    char   *pw_name;   /* username */
    char   *pw_passwd; /* user password (usually 'x' nowadays) */
    uid_t   pw_uid;    /* user ID */
    gid_t   pw_gid;    /* group ID */
    char   *pw_gecos;  /* user information (full name, etc.) */
    char   *pw_dir;    /* home directory */
    char   *pw_shell;  /* shell program */
};

int nanoOsGetpwnam_r(
  const char *name,
  struct passwd *pwd,
  char *buf,
  size_t buflen,
  struct passwd **result);
int nanoOsGetpwuid_r(
  uid_t uid,
  struct passwd *pwd,
  char *buf,
  size_t buflen,
  struct passwd **result);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_PWD_H

