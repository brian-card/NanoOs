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


/// @file Listing.c
///
/// @brief The stock file dialogs' listings:  taking each part a client
/// sends, then showing a directory's entries sorted, the places and the
/// Look in label, or carrying out the check of a path chosen, entered from
/// the Dialogs overlay only.

#include "WmFiles.h"

/// @fn static void wmShowLookIn(WmDialogJob *job, const char *path,
///   uint8_t errorNumber)
///
/// @brief Show a directory in a file dialog's Look in label, cut from the
/// left if it doesn't fit, or why it couldn't be shown.
///
/// @param job The job.
/// @param path The directory.
/// @param errorNumber Why it couldn't be read, or 0.
///
/// @return This function returns no value.
static void wmShowLookIn(WmDialogJob *job, const char *path,
  uint8_t errorNumber
) {
  static const uint8_t numbers[] = { EACCES, EPERM, ENOENT, ENOTDIR };
  static const char *const reasons[] = {
    "permission denied", "permission denied", "no such directory",
    "not a directory",
  };
  WmWidget *label = wmWidget(job, WM_DIALOG_LOOK_IN);
  char *text = (char*) malloc(WM_PATH_SIZE + 32);

  if ((label == NULL) || (text == NULL)) {
    free(text);
    return;
  }

  char reason[24];
  reason[0] = '\0';

  if (errorNumber != 0) {
    snprintf(reason, sizeof(reason), "error %u", (unsigned int) errorNumber);

    for (uint8_t ii = 0; ii < sizeof(numbers); ii++) {
      if (numbers[ii] == errorNumber) {
        strcpy(reason, reasons[ii]);
        break;
      }
    }
  }

  size_t columns = label->rect.w / job->state->display.builtInFont.glyphW;
  const char *prefix = (errorNumber != 0) ? "" : "Look in: ";
  size_t room = columns - strlen(prefix);

  if (errorNumber != 0) {
    room -= strlen(reason) + 2;
  }

  const char *shown = wmCutLeft(path, room);
  snprintf(text, WM_PATH_SIZE + 32, "%s%s%s%s%s", prefix,
    (shown != path) ? "..." : "", shown, (errorNumber != 0) ? ": " : "",
    reason);
  wmSetText(job, WM_DIALOG_LOOK_IN, text);
  free(text);
}

/// @fn static const uint8_t* wmEntryAt(const uint8_t *entries,
///   uint16_t size, uint16_t at)
///
/// @brief Check that an entry of a listing is whole.
///
/// @param entries The entries.
/// @param size The bytes at entries.
/// @param at Where the entry starts.
///
/// @return Returns the entry, or NULL if it runs past the end.
static const uint8_t* wmEntryAt(const uint8_t *entries, uint16_t size,
  uint16_t at
) {
  if (((at + 2) > size) || ((at + 2 + entries[at + 1]) > size)) {
    return NULL;
  }

  return &entries[at];
}

/// @fn static void wmKeepPlace(WmDialogJob *job, const uint8_t *entry)
///
/// @brief Keep a place a listing sent:  its kind and its NUL-ended path, the
/// home directory first and the rest in the order of their paths.
///
/// @param job The job.
/// @param entry The entry.
///
/// @return This function returns no value.
static void wmKeepPlace(WmDialogJob *job, const uint8_t *entry) {
  WmDialog *dialog = job->dialog;
  uint16_t added = (uint16_t) (entry[1] + 2);
  char *places = (char*) realloc(dialog->places, dialog->placesSize + added);

  if (places == NULL) {
    return;
  }

  uint16_t at = 0;

  while ((entry[0] != WIN_ENTRY_HOME) && (at < dialog->placesSize)) {
    const char *path = &places[at + 1];
    size_t length = strlen(path);
    uint8_t shorter = (uint8_t) ((length < entry[1]) ? length : entry[1]);
    int order = memcmp(&entry[2], path, shorter);

    if ((places[at] != WIN_ENTRY_HOME)
      && ((order < 0) || ((order == 0) && (entry[1] < length)))
    ) {
      break;
    }

    at = (uint16_t) (at + length + 2);
  }

  memmove(&places[at + added], &places[at], dialog->placesSize - at);
  places[at] = (char) entry[0];
  memcpy(&places[at + 1], &entry[2], entry[1]);
  places[at + 1 + entry[1]] = '\0';
  dialog->places = places;
  dialog->placesSize = (uint16_t) (dialog->placesSize + added);
}

/// @fn static int wmCompare(const uint8_t *first, const uint8_t *second)
///
/// @brief Order two entries as a file dialog lists them:  "..", then the
/// directories, then the files, each by name.
///
/// @param first An entry.
/// @param second Another.
///
/// @return Returns a negative number if first comes first, positive if
/// second does.
static int wmCompare(const uint8_t *first, const uint8_t *second) {
  bool firstUp = (first[1] == 2) && (memcmp(&first[2], "..", 2) == 0);
  bool secondUp = (second[1] == 2) && (memcmp(&second[2], "..", 2) == 0);

  if (firstUp != secondUp) {
    return firstUp ? -1 : 1;
  } else if (first[0] != second[0]) {
    return (first[0] == WIN_ENTRY_DIRECTORY) ? -1 : 1;
  }

  uint8_t shorter = (first[1] < second[1]) ? first[1] : second[1];
  int order = memcmp(&first[2], &second[2], shorter);

  if (order == 0) {
    order = first[1] - second[1];
  }

  return order;
}

/// @fn static bool wmShown(const WmDialog *dialog, const uint8_t *entry)
///
/// @brief Find whether a file dialog shows an entry:  every directory, and
/// a file if it ends in the suffix, or every file without one.
///
/// @param dialog The WmDialog.
/// @param entry The entry.
///
/// @return Returns true if it is shown, false otherwise.
static bool wmShown(const WmDialog *dialog, const uint8_t *entry) {
  if ((entry[0] == WIN_ENTRY_DIRECTORY) || (dialog->suffix == NULL)) {
    return true;
  }

  size_t length = strlen(dialog->suffix);

  return (entry[1] >= length)
    && (memcmp(&entry[2 + entry[1] - length], dialog->suffix, length) == 0);
}

/// @fn static void wmShowEntries(WmDialogJob *job)
///
/// @brief Show the entries of a file dialog's listing, sorted, each
/// directory with a '/' after it, as many as fit with "..." after them if
/// not all do, and select the first after "../" in an open dialog.
///
/// @param job The job.
///
/// @return This function returns no value.
static void wmShowEntries(WmDialogJob *job) {
  WmDialog *dialog = job->dialog;
  uint16_t count = 0;

  for (uint16_t at = 0; at < dialog->listingSize; ) {
    const uint8_t *entry = wmEntryAt(dialog->listing, dialog->listingSize, at);
    if (entry == NULL) {
      break;
    }

    count++;
    at = (uint16_t) (at + 2 + entry[1]);
  }

  const uint8_t **order = (const uint8_t**) malloc(
    (count + 1) * sizeof(const uint8_t*));
  char *items = (char*) malloc(WIN_LIST_ITEMS_MAX);

  if ((order == NULL) || (items == NULL)) {
    free(order);
    free(items);
    return;
  }

  uint16_t shown = 0;

  for (uint16_t at = 0; shown < count; ) {
    const uint8_t *entry = &dialog->listing[at];
    at = (uint16_t) (at + 2 + entry[1]);

    if (!wmShown(dialog, entry)) {
      count--;
      continue;
    }

    uint16_t place = shown++;

    while ((place > 0) && (wmCompare(order[place - 1], entry) > 0)) {
      order[place] = order[place - 1];
      place--;
    }

    order[place] = entry;
  }

  uint16_t size = 0;
  uint16_t listed = 0;
  bool up = false;

  for (; listed < shown; listed++) {
    const uint8_t *entry = order[listed];
    bool directory = (entry[0] == WIN_ENTRY_DIRECTORY);
    uint16_t itemSize = (uint16_t) (entry[1] + (directory ? 2 : 1));
    // The last item must leave room for "..." if any are left after it.
    uint16_t room = (uint16_t) (WIN_LIST_ITEMS_MAX - sizeof(WM_MORE));

    if ((listed == (shown - 1)) && (!dialog->truncated)) {
      room = WIN_LIST_ITEMS_MAX;
    }

    if ((size + itemSize) > room) {
      break;
    }

    memcpy(&items[size], &entry[2], entry[1]);
    size = (uint16_t) (size + entry[1]);

    if (directory) {
      items[size++] = '/';
    }

    items[size++] = '\0';
    up |= (listed == 0) && (entry[1] == 2)
      && (memcmp(&entry[2], "..", 2) == 0);
  }

  if ((listed < shown) || dialog->truncated) {
    memcpy(&items[size], WM_MORE, sizeof(WM_MORE));
    size = (uint16_t) (size + sizeof(WM_MORE));
  }

  uint16_t selection = WIN_NO_ITEM;
  uint16_t first = up ? 1 : 0;

  if ((dialog->kind == WIN_DIALOG_OPEN_FILE) && (first < listed)) {
    selection = first;
  }

  wmSet(job, WM_DIALOG_ENTRIES, WIN_PROP_ITEMS, 0, items, size);
  wmSet(job, WM_DIALOG_ENTRIES, WIN_PROP_VALUE, selection, NULL, 0);

  if ((selection != WIN_NO_ITEM) && (order[first][0] == WIN_ENTRY_FILE)) {
    char name[WM_PATH_SIZE];
    memcpy(name, &order[first][2], order[first][1]);
    name[order[first][1]] = '\0';
    wmSetText(job, WIN_DIALOG_FIELD, name);
  }

  free(order);
  free(items);
}

/// @fn static void wmShowDirectory(WmDialogJob *job, const char *path)
///
/// @brief Make a directory the one a file dialog shows, now that its
/// listing has come:  its entries and its Look in label, and the places
/// once the Files overlay shows them.
///
/// @param job The job.
/// @param path The directory, ending in '/'.
///
/// @return This function returns no value.
static void wmShowDirectory(WmDialogJob *job, const char *path) {
  WmDialog *dialog = job->dialog;
  size_t length = strlen(path);
  char *directory = (char*) malloc(length + 1);

  if (directory != NULL) {
    memcpy(directory, path, length + 1);
    free(dialog->directory);
    dialog->directory = directory;
  }

  dialog->started = true;
  wmShowEntries(job);
  wmShowLookIn(job, path, 0);
  job->placesShown = true;
}

/// @fn void* wmListingTake(void *args)
///
/// @brief Take a part of a listing a file dialog's client sent, and, with
/// the last part, show the directory it lists, or say what a check found
/// for the Files overlay to carry out.
///
/// @param args A pointer to the WmDialogJob, whose listing is set.
///
/// @return Always returns the provided pointer.
void* wmListingTake(void *args) {
  WmDialogJob *job = (WmDialogJob*) args;
  WmDialog *dialog = job->dialog;
  const WinDialogListingArgs *listing = job->listing;

  for (uint16_t at = 0; listing->entries != NULL; ) {
    const uint8_t *entry = wmEntryAt(listing->entries, listing->size, at);
    if (entry == NULL) {
      break;
    }

    uint16_t size = (uint16_t) (2 + entry[1]);
    at = (uint16_t) (at + size);

    if ((entry[0] == WIN_ENTRY_PLACE) || (entry[0] == WIN_ENTRY_HOME)) {
      if ((dialog->pending & WIN_LIST_PLACES) != 0) {
        wmKeepPlace(job, entry);
      }

      continue;
    } else if ((dialog->listingSize + size) > WM_LISTING_MAX) {
      dialog->truncated = true;
      continue;
    }

    if (dialog->listing == NULL) {
      dialog->listing = (uint8_t*) malloc(WM_LISTING_MAX);
      if (dialog->listing == NULL) {
        dialog->truncated = true;
        continue;
      }
    }

    memcpy(&dialog->listing[dialog->listingSize], entry, size);
    dialog->listingSize = (uint16_t) (dialog->listingSize + size);
  }

  if ((listing->flags & WIN_LISTING_FINAL) == 0) {
    return args;
  }

  uint8_t pending = dialog->pending;
  char *path = (char*) malloc(WM_PATH_SIZE);

  if (path == NULL) {
    return args;
  }

  dialog->pending = WM_NOT_PENDING;
  wmTextOf(job, WIN_DIALOG_PATH, path);

  if (((pending & WIN_LIST_CHECK) != 0) && (listing->errorNumber != 0)
    && (listing->errorNumber != ENOENT)
  ) {
    wmShowLookIn(job, path, listing->errorNumber);
  } else if ((pending & WIN_LIST_CHECK) != 0) {
    job->checked = true;
    job->errorNumber = listing->errorNumber;
    job->kind = ((dialog->listingSize >= 2) && (listing->errorNumber == 0))
      ? dialog->listing[0] : WIN_ENTRY_FILE;
  } else if (listing->errorNumber == 0) {
    wmShowDirectory(job, path);
  } else if ((!dialog->started) && (wmPlace(dialog, 0) != NULL)) {
    dialog->started = true;
    wmNavigate(job, &wmPlace(dialog, 0)[1]);
  } else {
    wmShowLookIn(job, path, listing->errorNumber);
    wmSet(job, WM_DIALOG_ENTRIES, WIN_PROP_ITEMS, 0, "", 0);
  }

  free(path);
  free(dialog->listing);
  dialog->listing = NULL;
  dialog->listingSize = 0;

  return args;
}
