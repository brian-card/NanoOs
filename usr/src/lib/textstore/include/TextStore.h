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

/// @file TextStore.h
///
/// @brief File-backed storage of lines of text:  an append-only text file
/// holding every line's text, and a block file holding the text-file offset
/// of each line in a linked list of fixed-size blocks, one of which is cached
/// in memory.  No memory is used per line.  The functions are inline so that
/// callers in any overlay can use them without an overlay call per line.

#ifndef TEXT_STORE_H
#define TEXT_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def TEXTSTORE_BUFFER_SIZE
///
/// @brief The size, in bytes, of the buffer text is moved through.
#define TEXTSTORE_BUFFER_SIZE 96

/// @def TEXTSTORE_BLOCK_LINES
///
/// @brief The number of line offsets a block holds.
#define TEXTSTORE_BLOCK_LINES 16

/// @def TEXTSTORE_NO_BLOCK
///
/// @brief The block number that refers to no block.
#define TEXTSTORE_NO_BLOCK 0xFFFF

/// @def TEXTSTORE_HEADER_SIZE
///
/// @brief The size, in bytes, of the area at the start of the block file that
/// a client can keep its own data in.
#define TEXTSTORE_HEADER_SIZE 128

/// @enum TextStoreError
///
/// @brief The errors a TextStore reports.  A client that keeps its own errors
/// in a TextStore's error member numbers them from NUM_TEXTSTORE_ERRORS.
typedef enum TextStoreError {
  TEXTSTORE_ERROR_NONE,
  TEXTSTORE_ERROR_IO,
  TEXTSTORE_ERROR_MEMORY,
  TEXTSTORE_ERROR_WRITE,
  NUM_TEXTSTORE_ERRORS
} TextStoreError;

/// @struct TextStoreBlock
///
/// @brief A block of the block file:  the text-file offsets of a run of
/// consecutive lines.
///
/// @param prev The number of the block holding the lines before these, or
///   TEXTSTORE_NO_BLOCK.
/// @param next The number of the block holding the lines after these, or
///   TEXTSTORE_NO_BLOCK.  For a free block, the next free block.
/// @param count The number of offsets in the block.
/// @param unused Padding that keeps offsets aligned.
/// @param offsets The text-file offset of each line.
typedef struct TextStoreBlock {
  uint16_t prev;
  uint16_t next;
  uint16_t count;
  uint16_t unused;
  uint32_t offsets[TEXTSTORE_BLOCK_LINES];
} TextStoreBlock;

/// @def TEXTSTORE_BLOCK_HEADER_SIZE
///
/// @brief The number of bytes of a TextStoreBlock before its offsets.
#define TEXTSTORE_BLOCK_HEADER_SIZE (4 * sizeof(uint16_t))

/// @struct TextStore
///
/// @brief The text file and block file of a sequence of lines.
///
/// @param text The text file that holds the text of every line.
/// @param textEnd The size, in bytes, of the text file.
/// @param blocks The block file that holds the line offsets.
/// @param numLines The number of lines.
/// @param head The number of the block holding the first line.
/// @param tail The number of the block holding the last line.
/// @param freeBlocks The number of the first free block.
/// @param numBlocks The number of blocks in the block file.
/// @param cached The number of the block in cache, or TEXTSTORE_NO_BLOCK.
/// @param cachedFirstLine The number of the first line in the cached block,
///   or 0 if it isn't known.
/// @param cacheDirty Whether cache has changed since it was read or written.
/// @param cache The cached block.
/// @param buffer A buffer of TEXTSTORE_BUFFER_SIZE bytes that text is moved
///   through, provided by the client.
/// @param error The TextStoreError of the operation that failed, or a code of
///   the client's own.
/// @param path The path of the text file.  The block file's is the same with
///   ".b" added.
typedef struct TextStore {
  FILE           *text;
  uint32_t        textEnd;
  FILE           *blocks;
  int             numLines;
  uint16_t        head;
  uint16_t        tail;
  uint16_t        freeBlocks;
  uint16_t        numBlocks;
  uint16_t        cached;
  int             cachedFirstLine;
  bool            cacheDirty;
  TextStoreBlock  cache;
  char           *buffer;
  uint8_t         error;
  char            path[16];
} TextStore;

/// @fn static inline bool textstoreSeek(TextStore *store, uint16_t block,
///   size_t position)
///
/// @brief Seek in the block file to a position in a block.
///
/// @param store The TextStore holding the block.
/// @param block The number of the block.
/// @param position The position in the block, in bytes.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreSeek(
  TextStore *store, uint16_t block, size_t position
) {
  if (fseek(store->blocks, (long) (TEXTSTORE_HEADER_SIZE
      + ((long) block * (long) sizeof(TextStoreBlock)) + position), SEEK_SET)
    != 0
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  return true;
}

/// @fn static inline bool textstoreWriteCache(TextStore *store)
///
/// @brief Write the cached block to the block file if it has changed.
///
/// @param store The TextStore whose cache to write.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreWriteCache(TextStore *store) {
  if (!store->cacheDirty) {
    return true;
  }
  if (!textstoreSeek(store, store->cached, 0)
    || (fwrite(&store->cache, sizeof(TextStoreBlock), 1, store->blocks) != 1)
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  store->cacheDirty = false;
  return true;
}

/// @fn static inline bool textstoreLoad(TextStore *store, uint16_t block,
///   int firstLine)
///
/// @brief Make a block the cached block.
///
/// @param store The TextStore holding the block.
/// @param block The number of the block.
/// @param firstLine The number of the block's first line, or 0 if unknown.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreLoad(
  TextStore *store, uint16_t block, int firstLine
) {
  if (block != store->cached) {
    if (!textstoreWriteCache(store)) {
      return false;
    }
    store->cached = TEXTSTORE_NO_BLOCK;
    if (!textstoreSeek(store, block, 0)
      || (fread(&store->cache, sizeof(TextStoreBlock), 1, store->blocks) != 1)
    ) {
      store->error = TEXTSTORE_ERROR_IO;
      return false;
    }
    store->cached = block;
  }
  store->cachedFirstLine = firstLine;
  return true;
}

/// @fn static inline bool textstoreReadLinks(TextStore *store,
///   uint16_t block, TextStoreBlock *links)
///
/// @brief Get a block's prev, next, and count, from the cache if it's cached.
///
/// @param store The TextStore holding the block.
/// @param block The number of the block.
/// @param links Set to the block's header.  Its offsets are not read.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreReadLinks(
  TextStore *store, uint16_t block, TextStoreBlock *links
) {
  if (block == TEXTSTORE_NO_BLOCK) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  } else if (block == store->cached) {
    memcpy(links, &store->cache, TEXTSTORE_BLOCK_HEADER_SIZE);
    return true;
  }
  if (!textstoreSeek(store, block, 0)
    || (fread(links, TEXTSTORE_BLOCK_HEADER_SIZE, 1, store->blocks) != 1)
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  return true;
}

/// @fn static inline bool textstoreSetLink(TextStore *store, uint16_t block,
///   bool next, uint16_t value)
///
/// @brief Set a block's prev or next, in the cache if it's cached.
///
/// @param store The TextStore holding the block.
/// @param block The number of the block.
/// @param next Whether to set next rather than prev.
/// @param value The block number to set it to.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreSetLink(
  TextStore *store, uint16_t block, bool next, uint16_t value
) {
  if (block == store->cached) {
    if (next) {
      store->cache.next = value;
    } else {
      store->cache.prev = value;
    }
    store->cacheDirty = true;
    return true;
  }
  if (!textstoreSeek(store, block, next ? sizeof(uint16_t) : 0)
    || (fwrite(&value, sizeof(value), 1, store->blocks) != 1)
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  return true;
}

/// @fn static inline uint16_t textstoreAllocBlock(TextStore *store)
///
/// @brief Take a block from the free list, or else the next new block.  The
/// caller must write a new block before allocating another, so that the block
/// file never has a gap.
///
/// @param store The TextStore to allocate a block in.
///
/// @return Returns the block's number, or TEXTSTORE_NO_BLOCK with store->error
/// set on failure.
static inline uint16_t textstoreAllocBlock(TextStore *store) {
  if (store->freeBlocks == TEXTSTORE_NO_BLOCK) {
    if (store->numBlocks == TEXTSTORE_NO_BLOCK) {
      store->error = TEXTSTORE_ERROR_MEMORY;
      return TEXTSTORE_NO_BLOCK;
    }
    return store->numBlocks++;
  }

  uint16_t block = store->freeBlocks;
  TextStoreBlock links;
  if (!textstoreReadLinks(store, block, &links)) {
    return TEXTSTORE_NO_BLOCK;
  }
  store->freeBlocks = links.next;
  return block;
}

/// @fn static inline bool textstoreWriteBlock(TextStore *store,
///   uint16_t block, uint16_t prev, uint16_t next, const uint32_t *offsets,
///   uint16_t count)
///
/// @brief Write a block that isn't cached, padding its unused offsets.
///
/// @param store The TextStore holding the block.
/// @param block The number of the block.
/// @param prev The block's prev.
/// @param next The block's next.
/// @param offsets The block's offsets.
/// @param count The number of offsets.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreWriteBlock(TextStore *store, uint16_t block,
  uint16_t prev, uint16_t next, const uint32_t *offsets, uint16_t count
) {
  static const uint32_t zeros[TEXTSTORE_BLOCK_LINES] = { 0 };
  uint16_t links[4] = { prev, next, count, 0 };
  if (!textstoreSeek(store, block, 0)
    || (fwrite(links, sizeof(links), 1, store->blocks) != 1)
    || (fwrite(offsets, sizeof(uint32_t), count, store->blocks) != count)
    || (fwrite(zeros, sizeof(uint32_t), TEXTSTORE_BLOCK_LINES - count,
      store->blocks) != (size_t) (TEXTSTORE_BLOCK_LINES - count))
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  return true;
}

/// @fn static inline int textstoreLocate(TextStore *store, int line)
///
/// @brief Make the block holding a line the cached block, walking the block
/// list from whichever of the cached block, the first block, and the last
/// block is nearest the line.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line, from 1 to store->numLines.
///
/// @return Returns the line's index in store->cache on success, -1 with
/// store->error set on failure.
static inline int textstoreLocate(TextStore *store, int line) {
  if ((store->cached != TEXTSTORE_NO_BLOCK) && (store->cachedFirstLine > 0)
    && (line >= store->cachedFirstLine)
    && (line < store->cachedFirstLine + store->cache.count)
  ) {
    return line - store->cachedFirstLine;
  }

  TextStoreBlock links;
  uint16_t block = store->head;
  int first = 1;
  int distance = line - 1;
  if (store->numLines - line < distance) {
    block = store->tail;
    if (!textstoreReadLinks(store, block, &links)) {
      return -1;
    }
    first = store->numLines - links.count + 1;
    distance = store->numLines - line;
  }
  int cacheDistance = line - store->cachedFirstLine;
  if (cacheDistance < 0) {
    cacheDistance = -cacheDistance;
  }
  if ((store->cached != TEXTSTORE_NO_BLOCK) && (store->cachedFirstLine > 0)
    && (cacheDistance < distance)
  ) {
    block = store->cached;
    first = store->cachedFirstLine;
  }

  while (1) {
    if (!textstoreReadLinks(store, block, &links)) {
      return -1;
    }
    if (line < first) {
      block = links.prev;
      if (!textstoreReadLinks(store, block, &links)) {
        return -1;
      }
      first -= links.count;
    } else if (line >= first + links.count) {
      first += links.count;
      block = links.next;
    } else {
      break;
    }
  }

  if (!textstoreLoad(store, block, first)) {
    return -1;
  }
  return line - first;
}

/// @fn static inline bool textstoreInit(TextStore *store, const char *mode)
///
/// @brief Open the text file and block file and start with no lines.
///
/// @param store The TextStore to initialize.  Its path is set.
/// @param mode The mode to open the files with.
///
/// @return Returns true on success, false with store->error set on failure,
/// in which case neither file is open.
static inline bool textstoreInit(TextStore *store, const char *mode) {
  static const uint8_t zeros[32] = { 0 };
  char blockPath[sizeof(store->path) + 2];
  strcpy(blockPath, store->path);
  strcat(blockPath, ".b");
  store->text = fopen(store->path, mode);
  store->blocks = fopen(blockPath, mode);
  bool ok = (store->text != NULL) && (store->blocks != NULL);
  for (size_t ii = 0; ok && (ii < TEXTSTORE_HEADER_SIZE); ii += sizeof(zeros)) {
    ok = (fwrite(zeros, sizeof(zeros), 1, store->blocks) == 1);
  }
  if (!ok) {
    if (store->text != NULL) {
      fclose(store->text);
      store->text = NULL;
    }
    if (store->blocks != NULL) {
      fclose(store->blocks);
      store->blocks = NULL;
    }
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }

  store->textEnd = 0;
  store->numLines = 0;
  store->head = TEXTSTORE_NO_BLOCK;
  store->tail = TEXTSTORE_NO_BLOCK;
  store->freeBlocks = TEXTSTORE_NO_BLOCK;
  store->numBlocks = 0;
  store->cached = TEXTSTORE_NO_BLOCK;
  store->cachedFirstLine = 0;
  store->cacheDirty = false;
  return true;
}

/// @fn static inline bool textstoreOpen(TextStore *store, const char *prefix)
///
/// @brief Create an empty text file and block file under /tmp, named with a
/// prefix and the process ID.
///
/// @param store The TextStore to open.
/// @param prefix The start of the text file's name.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreOpen(TextStore *store, const char *prefix) {
  // /tmp may already exist, so a failure here is only a problem if the files
  // can't be created below.
  mkdir("/tmp", 0777);
  snprintf(store->path, sizeof(store->path), "/tmp/%s%d", prefix, getpid());
  return textstoreInit(store, "w+");
}

/// @fn static inline bool textstoreReset(TextStore *store)
///
/// @brief Remove every line, and the client's header data.
///
/// @param store The TextStore to empty.
///
/// @return Returns true on success, false with store->error set on failure,
/// in which case the store has no files open.
static inline bool textstoreReset(TextStore *store) {
  // Reopening the files empties them, so every old line is gone.
  fclose(store->text);
  fclose(store->blocks);
  return textstoreInit(store, "w+");
}

/// @fn static inline void textstoreClose(TextStore *store)
///
/// @brief Close and remove the text file and block file.
///
/// @param store The TextStore to close.
///
/// @return This function returns no value.
static inline void textstoreClose(TextStore *store) {
  fclose(store->text);
  fclose(store->blocks);
  remove(store->path);
  strcat(store->path, ".b");
  remove(store->path);
  store->path[strlen(store->path) - 2] = '\0';
  store->text = NULL;
  store->blocks = NULL;
}

/// @fn static inline bool textstoreReadHeader(TextStore *store,
///   size_t position, void *data, size_t size)
///
/// @brief Read the client's data from the header area of the block file.
///
/// @param store The TextStore holding the data.
/// @param position The position of the data in the header area.
/// @param data Where to put the data.
/// @param size The size of the data, in bytes.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreReadHeader(
  TextStore *store, size_t position, void *data, size_t size
) {
  if ((fseek(store->blocks, (long) position, SEEK_SET) != 0)
    || (fread(data, size, 1, store->blocks) != 1)
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  return true;
}

/// @fn static inline bool textstoreWriteHeader(TextStore *store,
///   size_t position, const void *data, size_t size)
///
/// @brief Write the client's data to the header area of the block file.
///
/// @param store The TextStore to hold the data.
/// @param position The position of the data in the header area.
/// @param data The data.
/// @param size The size of the data, in bytes.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreWriteHeader(
  TextStore *store, size_t position, const void *data, size_t size
) {
  if ((fseek(store->blocks, (long) position, SEEK_SET) != 0)
    || (fwrite(data, size, 1, store->blocks) != 1)
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  return true;
}

/// @fn static inline uint32_t textstoreLineOffset(TextStore *store, int line)
///
/// @brief Get the text-file offset of a line.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line.
///
/// @return Returns the offset on success, 0 with store->error set on failure.
static inline uint32_t textstoreLineOffset(TextStore *store, int line) {
  int index = textstoreLocate(store, line);
  return (index < 0) ? 0 : store->cache.offsets[index];
}

/// @fn static inline void textstoreSetLineOffset(TextStore *store, int line,
///   uint32_t offset)
///
/// @brief Make a line's text the text at a text-file offset.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line.
/// @param offset The text-file offset of the line's new text.
///
/// @return This function returns no value.  On failure, store->error is set.
static inline void textstoreSetLineOffset(
  TextStore *store, int line, uint32_t offset
) {
  int index = textstoreLocate(store, line);
  if (index >= 0) {
    store->cache.offsets[index] = offset;
    store->cacheDirty = true;
  }
}

/// @fn static inline int textstoreFindLine(TextStore *store, uint32_t offset)
///
/// @brief Find the first line whose text is at a text-file offset.
///
/// @param store The TextStore to search.
/// @param offset The text-file offset.
///
/// @return Returns the number of the line, or 0 if no line has that offset or
/// the search failed, in which case store->error is set.
static inline int textstoreFindLine(TextStore *store, uint32_t offset) {
  int first = 1;
  for (uint16_t block = store->head; block != TEXTSTORE_NO_BLOCK;
    block = store->cache.next
  ) {
    if (!textstoreLoad(store, block, first)) {
      return 0;
    }
    for (int ii = 0; ii < store->cache.count; ii++) {
      if (store->cache.offsets[ii] == offset) {
        return first + ii;
      }
    }
    first += store->cache.count;
  }
  return 0;
}

/// @fn static inline bool textstoreAppendText(TextStore *store,
///   const char *text)
///
/// @brief Append text to the end of the text file.
///
/// @param store The TextStore to append to.
/// @param text The text to append.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreAppendText(TextStore *store, const char *text) {
  size_t length = strlen(text);
  if ((fseek(store->text, (long) store->textEnd, SEEK_SET) != 0)
    || (fwrite(text, 1, length, store->text) != length)
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }

  store->textEnd += length;
  return true;
}

/// @fn static inline bool textstoreInsertLine(TextStore *store, int after,
///   uint32_t offset)
///
/// @brief Insert a line.  A full block is split in half, except when the line
/// goes at its end, when the line starts a new block, so that lines added in
/// order fill whole blocks.
///
/// @param store The TextStore to insert into.
/// @param after The number of the line to insert after, 0 for the start.
/// @param offset The text-file offset of the line's text.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreInsertLine(
  TextStore *store, int after, uint32_t offset
) {
  if (store->numLines == 0) {
    uint16_t block = textstoreAllocBlock(store);
    if ((block == TEXTSTORE_NO_BLOCK) || !textstoreWriteCache(store)) {
      return false;
    }
    store->cached = block;
    store->cachedFirstLine = 1;
    store->cache.prev = TEXTSTORE_NO_BLOCK;
    store->cache.next = TEXTSTORE_NO_BLOCK;
    store->cache.count = 1;
    store->cache.unused = 0;
    store->cache.offsets[0] = offset;
    store->cacheDirty = true;
    if (!textstoreWriteCache(store)) {
      return false;
    }
    store->head = store->tail = block;
    store->numLines = 1;
    return true;
  }

  int position = textstoreLocate(store, (after == 0) ? 1 : after);
  if (position < 0) {
    return false;
  } else if (after > 0) {
    position++;
  }

  if (store->cache.count == TEXTSTORE_BLOCK_LINES) {
    uint16_t block = store->cached;
    uint16_t next = store->cache.next;
    uint16_t newBlock = textstoreAllocBlock(store);
    if (newBlock == TEXTSTORE_NO_BLOCK) {
      return false;
    }
    int split = (position == TEXTSTORE_BLOCK_LINES)
      ? TEXTSTORE_BLOCK_LINES : TEXTSTORE_BLOCK_LINES / 2;
    if (position <= split - (split == TEXTSTORE_BLOCK_LINES)) {
      // The line stays in the cached block; the upper half moves out.
      if (!textstoreWriteBlock(store, newBlock, block, next,
        &store->cache.offsets[split],
        (uint16_t) (TEXTSTORE_BLOCK_LINES - split))
      ) {
        return false;
      }
      store->cache.count = (uint16_t) split;
      store->cache.next = newBlock;
      store->cacheDirty = true;
    } else {
      // The line goes in the new block, which becomes the cached block.
      store->cache.count = (uint16_t) split;
      store->cache.next = newBlock;
      store->cacheDirty = true;
      if (!textstoreWriteCache(store)) {
        return false;
      }
      memmove(store->cache.offsets, &store->cache.offsets[split],
        (TEXTSTORE_BLOCK_LINES - split) * sizeof(uint32_t));
      store->cache.count = (uint16_t) (TEXTSTORE_BLOCK_LINES - split);
      store->cache.prev = block;
      store->cache.next = next;
      store->cached = newBlock;
      store->cachedFirstLine += split;
      position -= split;
      store->cacheDirty = true;
      // A new block is written at once so that the block file has no gap.
      if (!textstoreWriteCache(store)) {
        return false;
      }
    }
    if (next == TEXTSTORE_NO_BLOCK) {
      store->tail = newBlock;
    } else if (!textstoreSetLink(store, next, false, newBlock)) {
      return false;
    }
  }

  memmove(&store->cache.offsets[position + 1], &store->cache.offsets[position],
    (size_t) (store->cache.count - position) * sizeof(uint32_t));
  store->cache.offsets[position] = offset;
  store->cache.count++;
  store->cacheDirty = true;
  store->numLines++;
  return true;
}

/// @fn static inline uint16_t textstoreUnlinkCached(TextStore *store)
///
/// @brief Remove the cached block, which must be empty, from the block list
/// and put it on the free list.
///
/// @param store The TextStore holding the block.
///
/// @return Returns the number of the block that followed it, or
/// TEXTSTORE_NO_BLOCK if there was none or on failure, when store->error is
/// set.
static inline uint16_t textstoreUnlinkCached(TextStore *store) {
  uint16_t block = store->cached;
  uint16_t prev = store->cache.prev;
  uint16_t next = store->cache.next;
  store->cache.next = store->freeBlocks;
  store->cacheDirty = true;
  if (!textstoreWriteCache(store)) {
    return TEXTSTORE_NO_BLOCK;
  }
  store->freeBlocks = block;
  store->cached = TEXTSTORE_NO_BLOCK;

  if (prev == TEXTSTORE_NO_BLOCK) {
    store->head = next;
  } else if (!textstoreSetLink(store, prev, true, next)) {
    return TEXTSTORE_NO_BLOCK;
  }
  if (next == TEXTSTORE_NO_BLOCK) {
    store->tail = prev;
  } else if (!textstoreSetLink(store, next, false, prev)) {
    return TEXTSTORE_NO_BLOCK;
  }
  return next;
}

/// @fn static inline void textstoreDeleteLines(TextStore *store, int first,
///   int last)
///
/// @brief Remove a range of lines.  A block left empty goes on the free list.
///
/// @param store The TextStore to remove the lines from.
/// @param first The number of the first line to remove.
/// @param last The number of the last line to remove.
///
/// @return This function returns no value.  On failure, store->error is set.
static inline void textstoreDeleteLines(TextStore *store, int first, int last) {
  int remaining = last - first + 1;
  while (remaining > 0) {
    int index = textstoreLocate(store, first);
    if (index < 0) {
      return;
    }
    int count = store->cache.count - index;
    if (count > remaining) {
      count = remaining;
    }
    memmove(&store->cache.offsets[index], &store->cache.offsets[index + count],
      (size_t) (store->cache.count - index - count) * sizeof(uint32_t));
    store->cache.count -= (uint16_t) count;
    store->cacheDirty = true;
    store->numLines -= count;
    remaining -= count;
    if (store->cache.count == 0) {
      // The block started at first, so the one after it now does.
      uint16_t next = textstoreUnlinkCached(store);
      if (store->error != TEXTSTORE_ERROR_NONE) {
        return;
      }
      if ((next != TEXTSTORE_NO_BLOCK) && (remaining > 0)
        && !textstoreLoad(store, next, first)
      ) {
        return;
      }
    }
  }
}

/// @fn static inline uint16_t textstoreSplitAt(TextStore *store, int line)
///
/// @brief Make a line the first line of its block, splitting the block if
/// it isn't.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line, or store->numLines + 1 for the end.
///
/// @return Returns the number of the block the line starts, or
/// TEXTSTORE_NO_BLOCK for the end or on failure, when store->error is set.
static inline uint16_t textstoreSplitAt(TextStore *store, int line) {
  if (line > store->numLines) {
    return TEXTSTORE_NO_BLOCK;
  }
  int index = textstoreLocate(store, line);
  if (index <= 0) {
    return (index == 0) ? store->cached : TEXTSTORE_NO_BLOCK;
  }

  uint16_t next = store->cache.next;
  uint16_t newBlock = textstoreAllocBlock(store);
  if ((newBlock == TEXTSTORE_NO_BLOCK)
    || !textstoreWriteBlock(store, newBlock, store->cached, next,
      &store->cache.offsets[index], (uint16_t) (store->cache.count - index))
  ) {
    return TEXTSTORE_NO_BLOCK;
  }
  store->cache.count = (uint16_t) index;
  store->cache.next = newBlock;
  store->cacheDirty = true;
  if (next == TEXTSTORE_NO_BLOCK) {
    store->tail = newBlock;
  } else if (!textstoreSetLink(store, next, false, newBlock)) {
    return TEXTSTORE_NO_BLOCK;
  }
  return newBlock;
}

/// @fn static inline void textstoreMoveLines(TextStore *store, int first,
///   int last, int destination)
///
/// @brief Move a range of lines to after another line, which must not be in
/// the range.  The blocks are split at the range's ends and at the
/// destination, and the range's run of blocks is relinked, so the cost
/// doesn't grow with the number of lines moved.
///
/// @param store The TextStore holding the lines.
/// @param first The number of the first line to move.
/// @param last The number of the last line to move.
/// @param destination The number of the line to move them after, 0 for the
///   start.
///
/// @return This function returns no value.  On failure, store->error is set.
static inline void textstoreMoveLines(
  TextStore *store, int first, int last, int destination
) {
  if ((destination == first - 1) || (destination == last)) {
    return;
  }
  uint16_t start = textstoreSplitAt(store, first);
  uint16_t after = textstoreSplitAt(store, last + 1);
  uint16_t before = textstoreSplitAt(store, destination + 1);
  if (store->error != TEXTSTORE_ERROR_NONE) {
    return;
  }

  // Unlink the run of blocks from start to end.
  TextStoreBlock links;
  if (!textstoreReadLinks(store, start, &links)) {
    return;
  }
  uint16_t prev = links.prev;
  uint16_t end = store->tail;
  if (after != TEXTSTORE_NO_BLOCK) {
    if (!textstoreReadLinks(store, after, &links)) {
      return;
    }
    end = links.prev;
  }
  if (prev == TEXTSTORE_NO_BLOCK) {
    store->head = after;
  } else if (!textstoreSetLink(store, prev, true, after)) {
    return;
  }
  if (after == TEXTSTORE_NO_BLOCK) {
    store->tail = prev;
  } else if (!textstoreSetLink(store, after, false, prev)) {
    return;
  }

  // Link it back in ahead of the block that starts after the destination.
  uint16_t newPrev = store->tail;
  if (before != TEXTSTORE_NO_BLOCK) {
    if (!textstoreReadLinks(store, before, &links)) {
      return;
    }
    newPrev = links.prev;
  }
  if (newPrev == TEXTSTORE_NO_BLOCK) {
    store->head = start;
  } else if (!textstoreSetLink(store, newPrev, true, start)) {
    return;
  }
  if (before == TEXTSTORE_NO_BLOCK) {
    store->tail = end;
  } else if (!textstoreSetLink(store, before, false, end)) {
    return;
  }
  if (!textstoreSetLink(store, start, false, newPrev)
    || !textstoreSetLink(store, end, true, before)
  ) {
    return;
  }
  store->cachedFirstLine = 0;
}

/// @fn static inline long textstoreCopyText(TextStore *store,
///   uint32_t position, FILE *out, bool newline)
///
/// @brief Copy the line of text at a text-file offset to a stream, which may
/// be the text file itself.
///
/// @param store The TextStore holding the text.
/// @param position The text-file offset of the text.
/// @param out The stream to copy the text to.
/// @param newline Whether to copy the text's terminating newline.
///
/// @return Returns the number of bytes copied on success, -1 with
/// store->error set on failure.
static inline long textstoreCopyText(
  TextStore *store, uint32_t position, FILE *out, bool newline
) {
  long copied = 0;
  bool lineEnded = false;
  while (!lineEnded) {
    if ((fseek(store->text, (long) position, SEEK_SET) != 0)
      || (fgets(store->buffer, TEXTSTORE_BUFFER_SIZE, store->text) == NULL)
    ) {
      store->error = TEXTSTORE_ERROR_IO;
      return -1;
    }

    size_t length = strlen(store->buffer);
    position += length;
    lineEnded = (length > 0) && (store->buffer[length - 1] == '\n');
    if (lineEnded && !newline) {
      store->buffer[--length] = '\0';
    }

    if (out == store->text) {
      if (!textstoreAppendText(store, store->buffer)) {
        return -1;
      }
    } else if (fputs(store->buffer, out) == EOF) {
      store->error = TEXTSTORE_ERROR_WRITE;
      return -1;
    }
    copied += (long) length;
  }

  return copied;
}

/// @fn static inline long textstoreCopyLine(TextStore *store, int line,
///   FILE *out, bool newline)
///
/// @brief Copy the text of a line to a stream, which may be the text file
/// itself.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line to copy.
/// @param out The stream to copy the line to.
/// @param newline Whether to copy the line's terminating newline.
///
/// @return Returns the number of bytes copied on success, -1 with
/// store->error set on failure.
static inline long textstoreCopyLine(
  TextStore *store, int line, FILE *out, bool newline
) {
  uint32_t position = textstoreLineOffset(store, line);
  if (store->error != TEXTSTORE_ERROR_NONE) {
    return -1;
  }
  return textstoreCopyText(store, position, out, newline);
}

/// @fn static inline int textstoreReadLines(TextStore *store, FILE *in,
///   int after, bool inputMode, long *bytesRead)
///
/// @brief Read lines from a stream and insert them.
///
/// @param store The TextStore to insert the lines into.
/// @param in The stream to read from.
/// @param after The number of the line to insert the lines after.
/// @param inputMode Whether a line holding only "." ends the input, as it
///   does for text typed after the a, c, and i commands of ed.  On a failure
///   in input mode, the rest of the text is read and discarded so that none
///   of it is taken as commands.
/// @param bytesRead Incremented by the number of bytes read.
///
/// @return Returns the number of the last line inserted (after, if none
/// were) on success, -1 with store->error set on failure.
static inline int textstoreReadLines(
  TextStore *store, FILE *in, int after, bool inputMode, long *bytesRead
) {
  bool lineStart = true;
  while (fgets(store->buffer, TEXTSTORE_BUFFER_SIZE, in) == store->buffer) {
    if (inputMode && lineStart && (strcmp(store->buffer, ".\n") == 0)) {
      break;
    }

    size_t length = strlen(store->buffer);
    if (store->error == TEXTSTORE_ERROR_NONE) {
      if (lineStart && textstoreInsertLine(store, after, store->textEnd)) {
        after++;
      }
      if (store->error == TEXTSTORE_ERROR_NONE) {
        textstoreAppendText(store, store->buffer);
      }
      *bytesRead += (long) length;
      if ((store->error != TEXTSTORE_ERROR_NONE) && !inputMode) {
        return -1;
      }
    }
    lineStart = (length > 0) && (store->buffer[length - 1] == '\n');
  }
  if (store->error != TEXTSTORE_ERROR_NONE) {
    return -1;
  }

  if (!lineStart && !textstoreAppendText(store, "\n")) {
    return -1;
  }

  return after;
}

#ifdef __cplusplus
}
#endif

#endif // TEXT_STORE_H
