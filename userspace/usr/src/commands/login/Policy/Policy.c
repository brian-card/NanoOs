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

///////////////////////////////////////////////////////////////////////////////
///
/// @file              Policy.c
///
/// @brief             The parts of login that read /secure/shadow and turn
///                    /etc/capabilities into the user's directory capabilities.
///
///////////////////////////////////////////////////////////////////////////////

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dircap.h>
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#include "../include/LoginPolicy.h"

/// @def POLICY_LINE_SIZE
///
/// @brief The size of the buffers a line of a policy file and a directory name
/// are held in.
#define POLICY_LINE_SIZE 128

/// @def POLICY_TARGET_SIZE
///
/// @brief The number of bytes the entries of a user's target set can take.
#define POLICY_TARGET_SIZE 384

/// @var _defaultCapabilities
///
/// @brief What login uses when /etc/capabilities doesn't exist.
static const char _defaultCapabilities[] =
  "* home /home/\n"
  "root home /root/\n"
  "* rxls /usr/bin/\n"
  "* rx /usr/lib/\n"
  "* rls /etc/\n"
  "* rwcdls ~/\n"
  "* rwcdls /tmp/\n"
  "root rwcdls /etc/\n"
  "root rwcdlsx /usr/\n";

/// @struct PolicyEntry
///
/// @brief One packed entry of a target set.
///
/// @param mask The DIRCAP_* bits, or 0 once the entry has been refused.
/// @param length The length of name, not counting its NUL.
/// @param name The NUL-terminated directory, ending in '/'.
typedef struct PolicyEntry {
  uint8_t mask;
  uint8_t length;
  char    name[1];
} PolicyEntry;

/// @struct PolicyState
///
/// @brief What the steps of building and applying a user's set share.
///
/// @param file The open /etc/capabilities, or NULL to use the defaults.
/// @param cursor The next line of the defaults.
/// @param line The buffer for the current line.
/// @param expanded The buffer for a directory with its "~" expanded.
/// @param home The canonical home directory ending in '/', or NULL if invalid.
/// @param homeLength The length of home.
/// @param homeAllowed Whether a "home" line allows the home directory.
/// @param target The packed entries of the target set.
/// @param targetSize The number of bytes of target in use.
typedef struct PolicyState {
  FILE       *file;
  const char *cursor;
  char       *line;
  char       *expanded;
  char       *home;
  size_t      homeLength;
  bool        homeAllowed;
  uint8_t    *target;
  size_t      targetSize;
} PolicyState;

/// @fn static PolicyEntry* policyNextEntry(PolicyEntry *entry)
///
/// @brief Get the entry after one in the target set.
///
/// @param entry The entry.
///
/// @return Returns a pointer just past the entry.
static PolicyEntry* policyNextEntry(PolicyEntry *entry) {
  return (PolicyEntry*) &entry->name[entry->length + 1];
}

/// @fn static void policyRewind(PolicyState *state)
///
/// @brief Go back to the first line of the policy.
///
/// @param state The policy state.
///
/// @return This function returns no value.
static void policyRewind(PolicyState *state) {
  if (state->file != NULL) {
    fseek(state->file, 0, SEEK_SET);
  }
  state->cursor = _defaultCapabilities;
}

/// @fn static bool policyNextLine(PolicyState *state)
///
/// @brief Read the next line of the policy into state->line.  One byte of the
/// buffer stays free so that a directory can be given a trailing '/'.
///
/// @param state The policy state.
///
/// @return Returns true if a line was read, false at the end of the policy.
static bool policyNextLine(PolicyState *state) {
  if (state->file != NULL) {
    return fgets(state->line, POLICY_LINE_SIZE - 1, state->file) != NULL;
  } else if (*state->cursor == '\0') {
    return false;
  }

  size_t length = strcspn(state->cursor, "\n");
  memcpy(state->line, state->cursor, length);
  state->line[length] = '\0';
  state->cursor += length + 1;
  return true;
}

/// @fn static void policySkip(const char *reason, const char *directory)
///
/// @brief Report a line of the policy that is being skipped.
///
/// @param reason Why it is being skipped.
/// @param directory The directory the line names.
///
/// @return This function returns no value.
static void policySkip(const char *reason, const char *directory) {
  fprintf(stderr, "login: skipping capability for %s: %s\n", directory, reason);
}

/// @fn static size_t policyCanonicalName(char *name)
///
/// @brief Collapse an absolute directory name and end it in '/'.
///
/// @param name The name, in a buffer with room for one more byte.
///
/// @return Returns the length of the result.
static size_t policyCanonicalName(char *name) {
  size_t length = pathCollapse(name);
  if ((length == 0) || (name[length - 1] != '/')) {
    name[length++] = '/';
    name[length] = '\0';
  }
  return length;
}

/// @fn static bool policyLineFields(PolicyState *state, const char *username,
///   char **fields, uint8_t *mask)
///
/// @brief Split the current line and determine whether it applies to a user.
///
/// @param state The policy state.
/// @param username The user.
/// @param fields Where to store the line's three fields.
/// @param mask Where to store the line's DIRCAP_* bits, which are 0 for a
///   "home" line.
///
/// @return Returns true if the line applies to the user, false if not.
static bool policyLineFields(PolicyState *state, const char *username,
  char **fields, uint8_t *mask
) {
  uint8_t numFields = dirCapabilitySplitLine(state->line, fields);
  if (numFields == 0) {
    return false;
  } else if (numFields != 3) {
    policySkip("malformed line", state->line);
    return false;
  } else if (strcmp(fields[0], "supervisor") == 0) {
    policySkip("supervisor line in /etc/capabilities", fields[2]);
    return false;
  } else if ((strcmp(fields[0], "*") != 0)
    && (strcmp(fields[0], username) != 0)
  ) {
    return false;
  }

  *mask = 0;
  if (strcmp(fields[1], "home") != 0) {
    *mask = dirCapabilityMaskFromLetters(fields[1]);
    if (*mask == 0) {
      policySkip("invalid permissions", fields[2]);
      return false;
    }
  }
  return true;
}

/// @fn static void policyFindHome(PolicyState *state, const char *username)
///
/// @brief Determine whether a "home" line allows the user's home directory.
/// A "*" line needs it strictly below the line's directory, so that a home of
/// exactly /home can't cover every user.
///
/// @param state The policy state.
/// @param username The user.
///
/// @return This function returns no value.
static void policyFindHome(PolicyState *state, const char *username) {
  char *fields[3];
  uint8_t mask = 0;
  state->homeAllowed = false;
  policyRewind(state);
  // state->home comes from parsing /etc/passwd
  while ((state->home != NULL) && policyNextLine(state)) {
    if ((!policyLineFields(state, username, fields, &mask)) || (mask != 0)
      || (fields[2][0] != '/')
    ) {
      continue;
    }

    size_t length = policyCanonicalName(fields[2]);
    size_t minimum = (fields[0][0] == '*') ? (length + 1) : length;
    if ((state->homeLength >= minimum)
      && (strncmp(state->home, fields[2], length) == 0)
    ) {
      state->homeAllowed = true;
    }
  }
}

/// @fn static void policyTargetAdd(PolicyState *state, const char *name,
///   size_t length, uint8_t mask)
///
/// @brief Add a directory to the target set, combining it with any line that
/// already named it.
///
/// @param state The policy state.
/// @param name The canonical directory.
/// @param length The length of name.
/// @param mask The DIRCAP_* bits.
///
/// @return This function returns no value.
static void policyTargetAdd(PolicyState *state, const char *name,
  size_t length, uint8_t mask
) {
  PolicyEntry *entry = (PolicyEntry*) state->target;
  uint8_t *end = &state->target[state->targetSize];
  for (; (uint8_t*) entry < end; entry = policyNextEntry(entry)) {
    if ((entry->length == length) && (memcmp(entry->name, name, length) == 0)) {
      entry->mask |= mask;
      return;
    }
  }

  size_t size = offsetof(PolicyEntry, name) + length + 1;
  if ((length > UINT8_MAX) || (state->targetSize + size > POLICY_TARGET_SIZE)) {
    policySkip("too many capabilities", name);
    return;
  }
  entry->mask = mask;
  entry->length = (uint8_t) length;
  memcpy(entry->name, name, length + 1);
  state->targetSize += size;
}

/// @fn static void policyBuildTarget(PolicyState *state, const char *username)
///
/// @brief Build the user's target set from the "*" lines and the user's own.
///
/// @param state The policy state.
/// @param username The user.
///
/// @return This function returns no value.
static void policyBuildTarget(PolicyState *state, const char *username) {
  char *fields[3];
  uint8_t mask = 0;
  policyRewind(state);
  while (policyNextLine(state)) {
    if ((!policyLineFields(state, username, fields, &mask)) || (mask == 0)) {
      continue;
    }

    char *name = fields[2];
    if ((name[0] == '~') && ((name[1] == '/') || (name[1] == '\0'))) {
      if (!state->homeAllowed) {
        policySkip("home directory not allowed", name);
        continue;
      }
      size_t restLength = strlen(&name[1]);
      if (state->homeLength + restLength >= POLICY_LINE_SIZE - 1) {
        policySkip("name too long", name);
        continue;
      }
      memcpy(state->expanded, state->home, state->homeLength);
      strcpy(&state->expanded[state->homeLength],
        &name[(name[1] == '/') ? 2 : 1]);
      name = state->expanded;
    } else if (name[0] != '/') {
      policySkip("not an absolute path", name);
      continue;
    }

    size_t length = policyCanonicalName(name);
    policyTargetAdd(state, name, length, mask);
  }
}

/// @fn static void policyAddTarget(PolicyState *state)
///
/// @brief Add every entry of the target set to the process's set, longest name
/// first so that narrowing a directory can't refuse a subdirectory of it.
///
/// @param state The policy state.
///
/// @return This function returns no value.
static void policyAddTarget(PolicyState *state) {
  uint8_t *end = &state->target[state->targetSize];
  while (true) {
    PolicyEntry *longest = NULL;
    PolicyEntry *entry = (PolicyEntry*) state->target;
    for (; (uint8_t*) entry < end; entry = policyNextEntry(entry)) {
      // We're using DIRCAP_FULL_PATH to mark when we've already evaluated
      // an entry.
      if (((entry->mask & DIRCAP_FULL_PATH) == 0) && (entry->mask != 0)
        && ((longest == NULL) || (entry->length > longest->length))
      ) {
        longest = entry;
      }
    }
    if (longest == NULL) {
      break;
    }

    if (dircapAdd(longest->name, longest->mask) == 0) {
      longest->mask |= DIRCAP_FULL_PATH;
    } else {
      policySkip("not in the supervisor base set", longest->name);
      longest->mask = 0;
    }
  }
}

/// @fn static bool policyInTarget(PolicyState *state, const char *name,
///   size_t length)
///
/// @brief Determine whether a directory was granted by the target set.
///
/// @param state The policy state.
/// @param name The directory.
/// @param length The length of name.
///
/// @return Returns true if the target set holds the directory, false if not.
static bool policyInTarget(PolicyState *state, const char *name,
  size_t length
) {
  uint8_t *end = &state->target[state->targetSize];
  PolicyEntry *entry = (PolicyEntry*) state->target;
  for (; (uint8_t*) entry < end; entry = policyNextEntry(entry)) {
    if ((entry->mask != 0) && (entry->length == length)
      && (memcmp(entry->name, name, length) == 0)
    ) {
      return true;
    }
  }
  return false;
}

/// @fn static bool policyDropTheRest(PolicyState *state)
///
/// @brief Drop every capability the process holds that the user wasn't given.
///
/// @param state The policy state.
///
/// @return Returns true on success, false if any capability couldn't be
/// dropped.
static bool policyDropTheRest(PolicyState *state) {
  int size = dircapGet(NULL, 0);
  DirCapabilities *held = (size > 0) ? (DirCapabilities*) malloc(size) : NULL;
  if ((held == NULL) || (dircapGet(held, size) != size)) {
    free(held);
    return false;
  }

  bool success = true;
  DirCapability *capability = held->capabilities;
  for (uint8_t ii = 0; ii < held->numCapabilities; ii++) {
    if (((capability->mask & DIRCAP_FULL_PATH) != 0)
      || !policyInTarget(state, capability->name, capability->len)
    ) {
      memcpy(state->line, capability->name, capability->len);
      state->line[capability->len] = '\0';
      if (dircapDrop(state->line) != 0) {
        success = false;
      }
    }
    capability = (DirCapability*) &capability->name[capability->len];
  }

  free(held);
  return success;
}

/// @fn static char* policyCanonicalHome(const char *homeDirectory,
///   size_t *length)
///
/// @brief Make a copy of a home directory in canonical form.
///
/// @param homeDirectory The home directory from the passwd database.
/// @param length Where to store the length of the copy.
///
/// @return Returns the copy, which the caller must free, or NULL if the home
/// directory isn't absolute or memory ran out.
static char* policyCanonicalHome(const char *homeDirectory, size_t *length) {
  if ((homeDirectory == NULL) || (homeDirectory[0] != '/')) {
    return NULL;
  }

  char *home = (char*) malloc(strlen(homeDirectory) + 2);
  if (home != NULL) {
    strcpy(home, homeDirectory);
    *length = policyCanonicalName(home);
  }
  return home;
}

/// @fn void* loginApplyCapabilities(void *args)
///
/// @brief Replace the running process's directory capabilities with the ones
/// /etc/capabilities gives a user.
///
/// @param args A pointer to the LoginPolicyArgs.
///
/// @return Returns args on success, NULL if the process may still hold more
/// than the user was given.
void* loginApplyCapabilities(void *args) {
  LoginPolicyArgs *policyArgs = (LoginPolicyArgs*) args;
  void *returnValue = NULL;
  PolicyState state;
  memset(&state, 0, sizeof(state));

  errno = 0;
  state.file = fopen("/etc/capabilities", "r");
  if ((state.file == NULL) && (errno != ENOENT)) {
    return NULL;
  }

  state.line = (char*) malloc(POLICY_LINE_SIZE);
  state.expanded = (char*) malloc(POLICY_LINE_SIZE);
  state.target = (uint8_t*) malloc(POLICY_TARGET_SIZE);
  if ((state.line != NULL) && (state.expanded != NULL)
    && (state.target != NULL)
  ) {
    state.home = policyCanonicalHome(policyArgs->homeDirectory,
      &state.homeLength);
    policyFindHome(&state, policyArgs->username);
    policyBuildTarget(&state, policyArgs->username);
    policyAddTarget(&state);
    if (policyDropTheRest(&state)) {
      returnValue = args;
    }
  }

  if (state.file != NULL) {
    fclose(state.file);
  }
  free(state.home);
  free(state.target);
  free(state.expanded);
  free(state.line);
  return returnValue;
}

/// @fn void* loginLookupShadow(void *args)
///
/// @brief Find a user's password checksum in /secure/shadow.
///
/// @param args A pointer to the LoginPolicyArgs, whose checksum is set.
///
/// @return Returns args if the user has a line, NULL if not.
void* loginLookupShadow(void *args) {
  LoginPolicyArgs *policyArgs = (LoginPolicyArgs*) args;
  char *line = (char*) malloc(POLICY_LINE_SIZE);
  FILE *file = fopenFullPath("/secure/shadow", "r");
  void *returnValue = NULL;
  if ((line != NULL) && (file != NULL)) {
    size_t length = strlen(policyArgs->username);
    while ((returnValue == NULL)
      && (fgets(line, POLICY_LINE_SIZE, file) != NULL)
    ) {
      if ((strncmp(line, policyArgs->username, length) == 0)
        && (line[length] == ':')
      ) {
        policyArgs->checksum = strtol(&line[length + 1], NULL, 10);
        returnValue = args;
      }
    }
  }

  if (file != NULL) {
    fclose(file);
  }
  free(line);
  return returnValue;
}
