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

/// @file NanoOsGrp.c
///
/// @brief Kernel-side implementation of Unix grp functionality.

#include "string.h"
#include "NanoOsLibC.h"
#include "NanoOsGrp.h"
#include "../kernel/NanoOs.h"

/// @fn int populateGroup(struct group *grp, char *buf, size_t buflen,
///   const char *name, const char *passwd, gid_t gid, const char **members)
///
/// @brief Populate a struct group with the provided parameters.
///
/// @param grp A pointer to a struct group to populate.
/// @param buf A pointer to a character buffer to hold the string data.
/// @param buflen The number of bytes available in buf.
/// @param name The groupname.
/// @param passwd The group password (usually '!' nowadays).
/// @param gid The group ID.
/// @param members NULL-terminated array of pointers to names of group members.
///
/// @return Returns 0 on success, -errno on failure.
int populateGroup(struct group *grp, char *buf, size_t buflen,
  const char *name, const char *passwd, gid_t gid, const char **members
) {
  // This function is only called internally, so don't check for bad parameters.
  
  // Populate gr_name.
  int length = MIN((strlen(name) + 1), buflen);
  strncpy(buf, name, length);
  grp->gr_name = buf;
  buf += length;
  buflen -= length;
  
  // Populate gr_passwd.
  length = MIN((strlen(passwd) + 1), buflen);
  strncpy(buf, passwd, length);
  grp->gr_passwd = buf;
  buf += length;
  buflen -= length;
  
  // Populate gr_gid.
  grp->gr_gid = gid;
  
  // Populate gr_mem.
  grp->gr_mem = NULL;
  if (buflen > 0) {
    int numMembers = 0;
    for (; members[numMembers] != NULL; numMembers++);
    
    if (buflen >= ((numMembers + 1) * sizeof(char*))) {
      memset(buf, 0, ((numMembers + 1) * sizeof(char*)));
      grp->gr_mem = (char**) buf;
      buf += ((numMembers + 1) * sizeof(char*));
      buflen -= ((numMembers + 1) * sizeof(char*));
      
      for (int ii = 0; (ii < numMembers) && (buflen > 0); ii++) {
        length = MIN((strlen(members[ii]) + 1), buflen);
        strncpy(buf, members[ii], length);
        grp->gr_mem[ii] = buf;
        buf += length;
        buflen -= length;
      }
    }
  }
  
  return 0;
}

/// @var _rootGroupname
///
/// @brief Groupname of the root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _rootGroupname[] KEEP_IN_FLASH = "root";

/// @var _rootPasswd
///
/// @brief Password checksum for the root user as looked up by username.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _rootPasswd[] KEEP_IN_FLASH = "!";

/// @var _user1Groupname
///
/// @brief Groupname of the first non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user1Groupname[] KEEP_IN_FLASH = "user1";

/// @var _user1Passwd
///
/// @brief Password checksum for the first non-root user as looked up by
/// username.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user1Passwd[] KEEP_IN_FLASH = "!";

/// @var _user2Groupname
///
/// @brief Groupname of the second non-root user.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user2Groupname[] KEEP_IN_FLASH = "user2";

/// @var _user2Passwd
///
/// @brief Password checksum for the second non-root user as looked up by
/// username.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _user2Passwd[] KEEP_IN_FLASH = "!";

/// @fn int nanoOsGetgrnam_r(const char *name, struct group *grp, char *buf,
///   size_t buflen, struct group **result)
///
/// @brief NanoOs implementation of the standard POSIX getgrnam_r function to
/// get group information from the group database given a groupname.
///
/// @param name The groupname to look up in the group database.
/// @param grp A pointer to a struct group to populate.
/// @param buf A pointer to a character buffer that will hold all of the
///   strings in the populated group structure.
/// @param buflen The number of bytes available in the buf pointer.
/// @param result Double pointer to a struct group to populate on successful
///   lookup.
///
/// @return If the user is found, the result pointer is set to the provided grp
/// pointer and 0 is returned.  If no error occurs but the group is not found,
/// the result pointer is set to NULL and 0 is returned.  The result pointer is
/// set to NULL and an errno value is returned on error.
int nanoOsGetgrnam_r(
  const char *name,
  struct group *grp,
  char *buf,
  size_t buflen,
  struct group **result
) {
  if ((name == NULL) || (grp == NULL) || (buf == NULL) || (buflen == 0)
    || (result == NULL)
  ) {
    if (result != NULL) {
      *result = NULL;
    }
    return EIO;
  }
  
  int returnValue = 0;
  if (strcmp(name, _rootGroupname) == 0) {
    const char *members[] = {_rootGroupname, NULL};
    returnValue = populateGroup(grp, buf, buflen,
      /* name= */ _rootGroupname, /* passwd= */ _rootPasswd,
      /* gid= */ 0, /* members= */ members);
  } else if (strcmp(name, _user1Groupname) == 0) {
    const char *members[] = {_user1Groupname, NULL};
    returnValue = populateGroup(grp, buf, buflen,
      /* name= */ _user1Groupname, /* passwd= */ _user1Passwd,
      /* gid= */ 1, /* members= */ members);
  } else if (strcmp(name, _user2Groupname) == 0) {
    const char *members[] = {_user2Groupname, NULL};
    returnValue = populateGroup(grp, buf, buflen,
      /* name= */ _user2Groupname, /* passwd= */ _user2Passwd,
      /* gid= */ 2, /* members= */ members);
  } else {
    // Group not found.  Set result to NULL and return 0 as per spec.
    *result = NULL;
    return 0;
  }
  
  if (returnValue == 0) {
    *result = grp;
  }
  return -returnValue;
}

/// @fn int nanoOsGetgrgid_r(gid_t gid, struct group *grp, char *buf,
///   size_t buflen, struct group **result)
///
/// @brief NanoOs implementation of the standard POSIX getgrgid_r function to
/// get group information from the group database given a group ID.
///
/// @param gid The group ID to look up in the group database.
/// @param grp A pointer to a struct group to populate.
/// @param buf A pointer to a character buffer that will hold all of the
///   strings in the populated group structure.
/// @param buflen The number of bytes available in the buf pointer.
/// @param result Double pointer to a struct group to populate on successful
///   lookup.
///
/// @return If the user is found, the result pointer is set to the provided grp
/// pointer and 0 is returned.  If no error occurs but the group is not found,
/// the result pointer is set to NULL and 0 is returned.  The result pointer is
/// set to NULL and an errno value is returned on error.
int nanoOsGetgrgid_r(
  gid_t gid,
  struct group *grp,
  char *buf,
  size_t buflen,
  struct group **result
) {
  if ((grp == NULL) || (buf == NULL) || (buflen == 0) || (result == NULL)) {
    if (result != NULL) {
      *result = NULL;
    }
    return EIO;
  }
  
  int returnValue = 0;
  if (gid == 0) {
    const char *members[] = {_rootGroupname, NULL};
    returnValue = populateGroup(grp, buf, buflen,
      /* name= */ _rootGroupname, /* passwd= */ _rootPasswd,
      /* gid= */ 0, /* members= */ members);
  } else if (gid == 1) {
    const char *members[] = {_user1Groupname, NULL};
    returnValue = populateGroup(grp, buf, buflen,
      /* name= */ _user1Groupname, /* passwd= */ _user1Passwd,
      /* gid= */ 1, /* members= */ members);
  } else if (gid == 2) {
    const char *members[] = {_user2Groupname, NULL};
    returnValue = populateGroup(grp, buf, buflen,
      /* name= */ _user2Groupname, /* passwd= */ _user2Passwd,
      /* gid= */ 2, /* members= */ members);
  } else {
    // Group not found.  Set result to NULL and return 0 as per spec.
    *result = NULL;
    return 0;
  }
  
  if (returnValue == 0) {
    *result = grp;
  }
  return -returnValue;
}

