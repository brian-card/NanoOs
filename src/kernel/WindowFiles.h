///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.27.2026
///
/// @file              WindowFiles.h
///
/// @brief             The stock file dialogs of the window protocol's client
///                    library, and the directory reading that answers them
///                    in the application's own process, with its own
///                    directory capabilities.
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

#ifndef WINDOW_FILES_H
#define WINDOW_FILES_H

#include <dirent.h>
#include <stdlib.h>
#include <sys/dircap.h>

#include "Window.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @def WIN_PATH_CAPACITY
///
/// @brief The bytes of the buffer a list request's path is read into, its
/// terminator included.
#define WIN_PATH_CAPACITY 256

/// @def WIN_LISTING_CAPACITY
///
/// @brief The most bytes of entries one WIN_MSG_DIALOG_LISTING carries.
#define WIN_LISTING_CAPACITY 1024

/// @struct WinListing
///
/// @brief A listing being sent to a file dialog in parts.
///
/// @param session The connection.
/// @param dialog The dialog's window.
/// @param size The bytes of entries waiting to be sent.
/// @param entries The entries waiting to be sent.
typedef struct WinListing {
  WinSession *session;
  uint8_t     dialog;
  uint16_t    size;
  uint8_t    *entries;
} WinListing;

/// @fn static inline int winListingSend_(WinListing *listing, uint8_t flags,
///   uint8_t errorNumber)
///
/// @brief Send a file dialog the entries waiting, and start the next part.
///
/// @param listing The listing.
/// @param flags The WIN_LISTING_* bits.
/// @param errorNumber Why the directory couldn't be read, or 0.
///
/// @return Returns 0 on success, or a negative errno value on failure.
WIN_OUT_OF_LINE int winListingSend_(WinListing *listing, uint8_t flags,
  uint8_t errorNumber
) {
  MESSAGE_ARGS_ALLOCATE(WinDialogListingArgs, args);

  if (args == NULL) {
    return -ENOMEM;
  }

  WinSession *session = listing->session;

  args->dialog = listing->dialog;
  args->flags = flags;
  args->errorNumber = errorNumber;
  args->size = listing->size;
  args->entries = NULL;
  WinError status = WIN_ERR_NO_MEMORY;

  if (listing->size > 0) {
    args->entries = (const uint8_t*) MESSAGE_BUFFER_SHARE(listing->entries,
      listing->size, MESSAGE_BUFFER_IN, session->server);
  }

  if ((listing->size == 0) || (args->entries != NULL)) {
    status = winCall_(session, WIN_MSG_DIALOG_LISTING, args, sizeof(*args));
  }

  if (args->entries != NULL) {
    MESSAGE_BUFFER_UNSHARE(args->entries, listing->entries,
      MESSAGE_BUFFER_IN);
  }

  MESSAGE_ARGS_FREE(args);
  listing->size = 0;

  return winErrno(status);
}

/// @fn static inline void winListingAdd_(WinListing *listing, uint8_t kind,
///   const char *name, size_t length)
///
/// @brief Add an entry to a listing, sending the part waiting first if the
/// entry doesn't fit.
///
/// @param listing The listing.
/// @param kind The WIN_ENTRY_* kind.
/// @param name The name or path.
/// @param length Its bytes, at most 255 of which are sent.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winListingAdd_(WinListing *listing, uint8_t kind,
  const char *name, size_t length
) {
  if (length > UINT8_MAX) {
    length = UINT8_MAX;
  }

  if ((listing->size + 2 + length) > WIN_LISTING_CAPACITY) {
    winListingSend_(listing, 0, 0);
  }

  uint8_t *entry = &listing->entries[listing->size];
  entry[0] = kind;
  entry[1] = (uint8_t) length;
  memcpy(&entry[2], name, length);
  listing->size = (uint16_t) (listing->size + 2 + length);
}

/// @fn static inline bool winListable_(const DirCapabilities *set,
///   const char *directory, size_t length)
///
/// @brief Find whether a directory capability lets the application list a
/// directory:  one with DIRCAP_LIST whose name begins the directory's path.
///
/// @param set The application's capabilities.
/// @param directory The directory's absolute path, ending in '/'.
/// @param length The bytes at directory.
///
/// @return Returns true if it may be listed, false otherwise.
WIN_OUT_OF_LINE bool winListable_(const DirCapabilities *set,
  const char *directory, size_t length
) {
  const DirCapability *capability = set->capabilities;

  for (uint8_t ii = 0; ii < set->numCapabilities; ii++) {
    if (((capability->mask & (DIRCAP_LIST | DIRCAP_FULL_PATH)) == DIRCAP_LIST)
      && (capability->len <= length)
      && (memcmp(capability->name, directory, capability->len) == 0)
    ) {
      return true;
    }

    capability = (const DirCapability*) &capability->name[capability->len];
  }

  return false;
}

/// @fn static inline void winListPlaces_(WinListing *listing,
///   const DirCapabilities *set, uint8_t needed)
///
/// @brief Add the places to a listing:  each capability with the bits
/// needed that isn't inside another such capability, the home directory
/// marked.
///
/// @param listing The listing.
/// @param set The application's capabilities.
/// @param needed The DIRCAP_* bits a place must have.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winListPlaces_(WinListing *listing,
  const DirCapabilities *set, uint8_t needed
) {
  const char *home = getenv("HOME");
  size_t homeLength = (home != NULL) ? strlen(home) : 0;

  while ((homeLength > 0) && (home[homeLength - 1] == '/')) {
    homeLength--;
  }

  const DirCapability *place = set->capabilities;

  for (uint8_t ii = 0; ii < set->numCapabilities; ii++) {
    bool shown = ((place->mask & (needed | DIRCAP_FULL_PATH)) == needed);
    const DirCapability *other = set->capabilities;

    for (uint8_t jj = 0; shown && (jj < set->numCapabilities); jj++) {
      // A place inside another, or the same one again, is left out.
      if ((jj != ii)
        && ((other->mask & (needed | DIRCAP_FULL_PATH)) == needed)
        && ((other->len < place->len)
          || ((other->len == place->len) && (jj < ii)))
        && (memcmp(other->name, place->name, other->len) == 0)
      ) {
        shown = false;
      }

      other = (const DirCapability*) &other->name[other->len];
    }

    if (shown) {
      uint8_t kind = WIN_ENTRY_PLACE;

      if ((homeLength > 0) && (place->len == (homeLength + 1))
        && (memcmp(place->name, home, homeLength) == 0)
      ) {
        kind = WIN_ENTRY_HOME;
      }

      winListingAdd_(listing, kind, place->name, place->len);
    }

    place = (const DirCapability*) &place->name[place->len];
  }
}

/// @fn static inline uint8_t winListDirectory_(WinListing *listing,
///   const DirCapabilities *set, char *path, uint8_t flags)
///
/// @brief Add a directory's entries to a listing, ".." first if the
/// application may list the parent, or, for WIN_LIST_CHECK, the path's own
/// entry from its parent directory.
///
/// @param listing The listing.
/// @param set The application's capabilities.
/// @param path The absolute path, which may be cut while it is read.
/// @param flags The request's WIN_LIST_* bits.
///
/// @return Returns 0, or the errno value for why it couldn't be read.
WIN_OUT_OF_LINE uint8_t winListDirectory_(WinListing *listing,
  const DirCapabilities *set, char *path, uint8_t flags
) {
  size_t length = strlen(path);

  while ((length > 1) && (path[length - 1] == '/')) {
    path[--length] = '\0';
  }

  char *slash = strrchr(path, '/');
  if ((path[0] != '/') || (slash == NULL)) {
    return EINVAL;
  }

  size_t parentLength = ((size_t) (slash - path)) + 1;
  const char *name = slash + 1;

  if ((flags & WIN_LIST_CHECK) != 0) {
    if (*name == '\0') {
      winListingAdd_(listing, WIN_ENTRY_DIRECTORY, "/", 1);
      return 0;
    }

    char saved = path[parentLength];

    path[parentLength] = '\0';
    DIR *directory = opendir(path);

    path[parentLength] = saved;

    if (directory == NULL) {
      return (uint8_t) errno;
    }

    uint8_t result = ENOENT;
    struct dirent *entry;

    while ((result == ENOENT) && ((entry = readdir(directory)) != NULL)) {
      if (strcmp(entry->d_name, name) == 0) {
        winListingAdd_(listing, (entry->d_type == DT_DIR)
          ? WIN_ENTRY_DIRECTORY : WIN_ENTRY_FILE, name, strlen(name));
        result = 0;
      }
    }

    closedir(directory);

    return result;
  }

  DIR *directory = opendir(path);
  if (directory == NULL) {
    return (uint8_t) errno;
  }

  if ((*name != '\0') && winListable_(set, path, parentLength)) {
    winListingAdd_(listing, WIN_ENTRY_DIRECTORY, "..", 2);
  }

  struct dirent *entry;

  while ((entry = readdir(directory)) != NULL) {
    if ((strcmp(entry->d_name, ".") != 0)
      && (strcmp(entry->d_name, "..") != 0)
    ) {
      winListingAdd_(listing, (entry->d_type == DT_DIR)
        ? WIN_ENTRY_DIRECTORY : WIN_ENTRY_FILE, entry->d_name,
        strlen(entry->d_name));
    }
  }

  closedir(directory);

  return 0;
}

/// @fn static inline int winAnswerListRequest(WinSession *session,
///   const WinEvent *event)
///
/// @brief Answer a file dialog's WIN_EVENT_DIALOG_LIST_REQUEST in the
/// application's own process:  read the directory it names with the
/// application's own user and directory capabilities, and send the places
/// first if it asks for them.
///
/// @param session The connection.
/// @param event The request.
///
/// @return Returns 0 on success, or a negative errno value if the answer
/// couldn't be sent.
WIN_OUT_OF_LINE int winAnswerListRequest(WinSession *session,
  const WinEvent *event
) {
  WinListing listing = { session, event->window, 0, NULL };
  char *path = (char*) malloc(WIN_PATH_CAPACITY);
  int setSize = dircapGet(NULL, 0);
  DirCapabilities *set = (setSize > 0) ? (DirCapabilities*) malloc(setSize)
    : NULL;
  uint8_t flags = (uint8_t) event->value;
  uint8_t errorNumber = ENOMEM;

  listing.entries = (uint8_t*) malloc(WIN_LISTING_CAPACITY);

  if ((path != NULL) && (set != NULL) && (listing.entries != NULL)
    && (dircapGet(set, (size_t) setSize) == setSize)
    && (winGetText(session, event->window, WIN_DIALOG_PATH, path,
      WIN_PATH_CAPACITY) >= 0)
  ) {
    if ((flags & WIN_LIST_PLACES) != 0) {
      uint8_t needed = DIRCAP_LIST;

      if ((flags & WIN_LIST_CREATE) != 0) {
        needed |= DIRCAP_CREATE;
      }

      winListPlaces_(&listing, set, needed);
    }

    errorNumber = winListDirectory_(&listing, set, path, flags);
  }

  int rc = winListingSend_(&listing, WIN_LISTING_FINAL, errorNumber);

  free(listing.entries);
  free(set);
  free(path);

  return rc;
}

/// @fn static inline int winFileDialog_(WinSession *session, uint8_t owner,
///   uint8_t kind, const char *title, char *path, uint8_t size,
///   const char *suffix)
///
/// @brief Run a stock open or save dialog owned by one of the caller's
/// windows until it is answered, answering its list requests and keeping
/// the events about other windows for winNextEvent.
///
/// @param session The connection.
/// @param owner The window that owns the dialog.
/// @param kind WIN_DIALOG_OPEN_FILE or WIN_DIALOG_SAVE_FILE.
/// @param title The title, or NULL.
/// @param path The absolute path the dialog starts at, a directory ending in
///   '/' or a file, or "" for the first place; and where the path chosen
///   goes on OK, NUL-ended; unchanged otherwise.
/// @param size The bytes at path.
/// @param suffix The only suffix shown, or NULL or "" for every file.
///
/// @return Returns WIN_RESULT_OK or WIN_RESULT_CANCEL, or a negative errno
/// value on failure.
WIN_OUT_OF_LINE int winFileDialog_(WinSession *session, uint8_t owner,
  uint8_t kind, const char *title, char *path, uint8_t size,
  const char *suffix
) {
  if (size == 0) {
    return -EINVAL;
  }

  int dialog = winOpenDialog(session, owner, kind, WIN_ICON_NONE,
    WIN_BUTTONS_OK_CANCEL, (uint8_t) (size - 1), title, NULL, path, suffix);
  if (dialog < 0) {
    return dialog;
  }

  int result = winDialogResult_(session, (uint8_t) dialog,
    winAnswerListRequest);
  if (result == WIN_RESULT_OK) {
    int rc = winGetText(session, (uint8_t) dialog, WIN_DIALOG_PATH, path,
      size);
    if (rc < 0) {
      result = rc;
    }
  }

  winDestroyWindow(session, (uint8_t) dialog);

  return result;
}

/// @fn static inline int winOpenFile(WinSession *session, uint8_t owner,
///   const char *title, char *path, uint8_t size, const char *suffix)
///
/// @brief Ask for a file to open in a stock dialog:  winFileDialog_ for
/// WIN_DIALOG_OPEN_FILE.
///
/// @param session The connection.
/// @param owner The window that owns the dialog.
/// @param title The title, or NULL.
/// @param path Where the dialog starts, and where the path chosen goes.
/// @param size The bytes at path.
/// @param suffix The only suffix shown, or NULL or "" for every file.
///
/// @return Returns WIN_RESULT_OK or WIN_RESULT_CANCEL, or a negative errno
/// value on failure.
static inline int winOpenFile(WinSession *session, uint8_t owner,
  const char *title, char *path, uint8_t size, const char *suffix
) {
  return winFileDialog_(session, owner, WIN_DIALOG_OPEN_FILE, title, path,
    size, suffix);
}

/// @fn static inline int winSaveFile(WinSession *session, uint8_t owner,
///   const char *title, char *path, uint8_t size, const char *suffix)
///
/// @brief Ask for a file to save to in a stock dialog, which asks before
/// a file that exists is chosen:  winFileDialog_ for WIN_DIALOG_SAVE_FILE.
///
/// @param session The connection.
/// @param owner The window that owns the dialog.
/// @param title The title, or NULL.
/// @param path Where the dialog starts, and where the path chosen goes.
/// @param size The bytes at path.
/// @param suffix The only suffix shown, or NULL or "" for every file.
///
/// @return Returns WIN_RESULT_OK or WIN_RESULT_CANCEL, or a negative errno
/// value on failure.
static inline int winSaveFile(WinSession *session, uint8_t owner,
  const char *title, char *path, uint8_t size, const char *suffix
) {
  return winFileDialog_(session, owner, WIN_DIALOG_SAVE_FILE, title, path,
    size, suffix);
}

#ifdef __cplusplus
}
#endif

#endif // WINDOW_FILES_H
