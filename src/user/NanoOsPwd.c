////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2026 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

#include "string.h"
#include "NanoOsLibC.h"
#include "NanoOsPwd.h"
#include "../kernel/NanoOs.h"

/// @fn int populatePasswd(struct passwd *pwd, char *buf, size_t buflen,
///   const char *name, const char *passwd, uid_t uid, gid_t gid,
///   const char *gecos, const char *dir, const char *shell)
///
/// @brief Populate a struct passwd with the provided parameters.
///
/// @param pwd A pointer to a struct passwd to populate.
/// @param buf A pointer to a character buffer to hold the string data.
/// @param buflen The number of bytes available in buf.
/// @param name The username.
/// @param passwd The user password (usually 'x' nowadays).
/// @param uid The user ID.
/// @param gid The group ID.
/// @param gecos Auxiliary user information (full name, etc.).
/// @param dir The user's home directory.
/// @param shell The user's shell program.
///
/// @return Returns 0 on success, -errno on failure.
int populatePasswd(struct passwd *pwd, char *buf, size_t buflen,
  const char *name, const char *passwd, uid_t uid, gid_t gid,
  const char *gecos, const char *dir, const char *shell
) {
  // This function is only called internally, so don't check for bad parameters.
  
  // Populate pw_name.
  int length = MIN((strlen(name) + 1), buflen);
  strncpy(buf, name, length);
  pwd->pw_name = buf;
  buf += length;
  buflen -= length;
  
  // Populate pw_passwd.
  length = MIN((strlen(passwd) + 1), buflen);
  strncpy(buf, passwd, length);
  pwd->pw_passwd = buf;
  buf += length;
  buflen -= length;
  
  // Populate pw_uid.
  pwd->pw_uid = uid;
  
  // Populate pw_gid.
  pwd->pw_gid = gid;
  
  // Populate pw_gecos.
  length = MIN((strlen(gecos) + 1), buflen);
  strncpy(buf, gecos, length);
  pwd->pw_gecos = buf;
  buf += length;
  buflen -= length;
  
  // Populate pw_dir.
  length = MIN((strlen(dir) + 1), buflen);
  strncpy(buf, dir, length);
  pwd->pw_dir = buf;
  buf += length;
  buflen -= length;
  
  // Populate pw_shell.
  length = MIN((strlen(shell) + 1), buflen);
  strncpy(buf, shell, length);
  pwd->pw_shell = buf;
  buf += length;
  buflen -= length;
  
  return 0;
}

/// @var _defaultShell
///
/// @brief Shell program assigned to every user account.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _defaultShell[] KEEP_IN_FLASH = "/usr/bin/mush";

/// @var _rootUsername
///
/// @brief Username of the root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _rootUsername[] KEEP_IN_FLASH = "root";

/// @var _rootGecos
///
/// @brief Auxiliary user information for the root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _rootGecos[] KEEP_IN_FLASH = "Root User";

/// @var _rootHome
///
/// @brief Home directory of the root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _rootHome[] KEEP_IN_FLASH = "/root";

/// @var _rootPasswdShort
///
/// @brief Password checksum for the root user as looked up by username.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _rootPasswdShort[] KEEP_IN_FLASH = "1356";

/// @var _rootPasswdLong
///
/// @brief Password checksum for the root user as looked up by user ID.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _rootPasswdLong[] KEEP_IN_FLASH = "rootroot";

/// @var _user1Username
///
/// @brief Username of the first non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user1Username[] KEEP_IN_FLASH = "user1";

/// @var _user1Gecos
///
/// @brief Auxiliary user information for the first non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user1Gecos[] KEEP_IN_FLASH = "User 1";

/// @var _user1Home
///
/// @brief Home directory of the first non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user1Home[] KEEP_IN_FLASH = "/home/user1";

/// @var _user1PasswdShort
///
/// @brief Password checksum for the first non-root user as looked up by
/// username.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user1PasswdShort[] KEEP_IN_FLASH = "1488";

/// @var _user1PasswdLong
///
/// @brief Password checksum for the first non-root user as looked up by
/// user ID.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user1PasswdLong[] KEEP_IN_FLASH = "user1user1";

/// @var _user2Username
///
/// @brief Username of the second non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user2Username[] KEEP_IN_FLASH = "user2";

/// @var _user2Gecos
///
/// @brief Auxiliary user information for the second non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user2Gecos[] KEEP_IN_FLASH = "User 2";

/// @var _user2Home
///
/// @brief Home directory of the second non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user2Home[] KEEP_IN_FLASH = "/home/user2";

/// @var _user2PasswdShort
///
/// @brief Password checksum for the second non-root user as looked up by
/// username.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user2PasswdShort[] KEEP_IN_FLASH = "1491";

/// @var _user2PasswdLong
///
/// @brief Password checksum for the second non-root user as looked up by
/// user ID.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user2PasswdLong[] KEEP_IN_FLASH = "user2user2";

/// @fn int nanoOsGetpwnam_r(const char *name, struct passwd *pwd,
///   char *buf, size_t buflen, struct passwd **result)
///
/// @brief NanoOs implementation of the standard POSIX getpwnam_r function to
/// get user information from the passwd database given a username.
///
/// @param name The username to look up in the passwd database.
/// @param pwd A pointer to a struct passwd to populate.
/// @param buf A pointer to a character buffer that will hold all of the
///   strings in the populated passwd structure.
/// @param buflen The number of bytes available in the buf pointer.
/// @param result Double pointer to a struct passwd to populate on successful
///   lookup.
///
/// @return If the user is found, the result pointer is set to the provided pwd
/// pointer and 0 is returned.  If no error occurs but the user is not found,
/// the result pointer is set to NULL and 0 is returned.  The result pointer is
/// set to NULL and an errno value is returned on error.
int nanoOsGetpwnam_r(
  const char *name,
  struct passwd *pwd,
  char *buf,
  size_t buflen,
  struct passwd **result
) {
  if ((name == NULL) || (pwd == NULL) || (buf == NULL) || (buflen == 0)) {
    if (result != NULL) {
      *result = NULL;
    }
    return EIO;
  }
  
  int returnValue = 0;
  if (strcmp(name, _rootUsername) == 0) {
    returnValue = populatePasswd(pwd, buf, buflen,
    /* name= */ _rootUsername, /* passwd= */ _rootPasswdShort,
    /* uid= */ 0, /* gid= */ 0,
    /* gecos= */ _rootGecos, /* dir= */ _rootHome,
    /* shell= */ _defaultShell);
  } else if (strcmp(name, _user1Username) == 0) {
    returnValue = populatePasswd(pwd, buf, buflen,
    /* name= */ _user1Username, /* passwd= */ _user1PasswdShort,
    /* uid= */ 1, /* gid= */ 1,
    /* gecos= */ _user1Gecos, /* dir= */ _user1Home,
    /* shell= */ _defaultShell);
  } else if (strcmp(name, _user2Username) == 0) {
    returnValue = populatePasswd(pwd, buf, buflen,
    /* name= */ _user2Username, /* passwd= */ _user2PasswdShort,
    /* uid= */ 2, /* gid= */ 2,
    /* gecos= */ _user2Gecos, /* dir= */ _user2Home,
    /* shell= */ _defaultShell);
  } else {
    // User not found.  Set result to NULL and return 0 as per spec.
    if (result != NULL) {
      *result = NULL;
    }
    return 0;
  }
  
  if ((returnValue == 0) && (result != NULL)) {
    *result = pwd;
  }
  return -returnValue;
}

int nanoOsGetpwuid_r(
  uid_t uid,
  struct passwd *pwd,
  char *buf,
  size_t buflen,
  struct passwd **result
) {
  if ((pwd == NULL) || (buf == NULL) || (buflen == 0)) {
    if (result != NULL) {
      *result = NULL;
    }
    return EIO;
  }
  
  int returnValue = 0;
  if (uid == 0) {
    returnValue = populatePasswd(pwd, buf, buflen,
    /* name= */ _rootUsername, /* passwd= */ _rootPasswdLong,
    /* uid= */ 0, /* gid= */ 0,
    /* gecos= */ _rootGecos, /* dir= */ _rootHome,
    /* shell= */ _defaultShell);
  } else if (uid == 1) {
    returnValue = populatePasswd(pwd, buf, buflen,
    /* name= */ _user1Username, /* passwd= */ _user1PasswdLong,
    /* uid= */ 1, /* gid= */ 1,
    /* gecos= */ _user1Gecos, /* dir= */ _user1Home,
    /* shell= */ _defaultShell);
  } else if (uid == 2) {
    returnValue = populatePasswd(pwd, buf, buflen,
    /* name= */ _user2Username, /* passwd= */ _user2PasswdLong,
    /* uid= */ 2, /* gid= */ 2,
    /* gecos= */ _user2Gecos, /* dir= */ _user2Home,
    /* shell= */ _defaultShell);
  } else {
    // User not found.  Set result to NULL and return 0 as per spec.
    if (result != NULL) {
      *result = NULL;
    }
    return 0;
  }
  
  if ((returnValue == 0) && (result != NULL)) {
    *result = pwd;
  }
  return -returnValue;
}

