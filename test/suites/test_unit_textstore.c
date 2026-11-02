///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_textstore.c
///
/// @brief             Unit tests (no kernel boot) for the textstore library's
///                    paged line table, run against real files on the host.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../usr/src/lib/textstore/include/TextStore.h"

/// @def MODEL_MAX_LINES
///
/// @brief The most lines the model of the line table holds.
#define MODEL_MAX_LINES 1024

/// @var model
///
/// @brief The offset each line should have.  Line n is at index n - 1.
static uint32_t model[MODEL_MAX_LINES];

/// @var modelLines
///
/// @brief The number of lines in model.
static int modelLines;

/// @var textBuffer
///
/// @brief The buffer the TextStore under test moves text through.
static char textBuffer[TEXTSTORE_BUFFER_SIZE];

/// @var randomState
///
/// @brief The state of the pseudo-random number generator.
static uint32_t randomState;

/// @fn static int randomBelow(int limit)
///
/// @brief Get a pseudo-random number, the same sequence on every run.
///
/// @param limit One more than the largest number to return.
///
/// @return Returns a number from 0 to limit - 1.
static int randomBelow(int limit) {
  randomState = (randomState * 1103515245u) + 12345u;
  return (int) ((randomState >> 8) % (uint32_t) limit);
}

/// @fn static bool openStore(TextStore *store)
///
/// @brief Open an empty TextStore and an empty model of it.
///
/// @param store The TextStore to open.
///
/// @return Returns true on success, false on failure.
static bool openStore(TextStore *store) {
  memset(store, 0, sizeof(*store));
  store->buffer = textBuffer;
  modelLines = 0;
  randomState = 1;
  return textstoreOpen(store, "tst");
}

/// @fn static bool structureIsSound(TextStore *store)
///
/// @brief Walk the block list, checking that every block holds 1 to
/// TEXTSTORE_BLOCK_LINES lines, that prev and next agree, that the counts add
/// up to numLines, and that head and tail are its ends.
///
/// @param store The TextStore to check.
///
/// @return Returns true if the list is sound, false otherwise.
static bool structureIsSound(TextStore *store) {
  TextStoreBlock links;
  uint16_t prev = TEXTSTORE_NO_BLOCK;
  int lines = 0;
  for (uint16_t block = store->head; block != TEXTSTORE_NO_BLOCK;
    block = links.next
  ) {
    if (!textstoreReadLinks(store, block, &links)
      || (links.prev != prev) || (links.count == 0)
      || (links.count > TEXTSTORE_BLOCK_LINES)
      || (block >= store->numBlocks)
    ) {
      return false;
    }
    lines += links.count;
    prev = block;
  }
  return (prev == store->tail) && (lines == store->numLines);
}

/// @fn static bool storeMatchesModel(TextStore *store)
///
/// @brief Check that a TextStore holds the model's lines and a sound block
/// list, with no error.
///
/// @param store The TextStore to check.
///
/// @return Returns true if it does, false otherwise.
static bool storeMatchesModel(TextStore *store) {
  if ((store->numLines != modelLines) || !structureIsSound(store)) {
    return false;
  }
  for (int line = 1; line <= modelLines; line++) {
    if (textstoreLineOffset(store, line) != model[line - 1]) {
      return false;
    }
  }
  return store->error == TEXTSTORE_ERROR_NONE;
}

/// @fn static void modelMove(int first, int last, int destination)
///
/// @brief Move a range of the model's lines to after another line.
///
/// @param first The number of the first line to move.
/// @param last The number of the last line to move.
/// @param destination The number of the line to move them after.
///
/// @return This function returns no value.
static void modelMove(int first, int last, int destination) {
  static uint32_t moved[MODEL_MAX_LINES];
  int count = last - first + 1;
  memcpy(moved, &model[first - 1], (size_t) count * sizeof(uint32_t));
  memmove(&model[first - 1], &model[last],
    (size_t) (modelLines - last) * sizeof(uint32_t));
  int at = (destination >= last) ? destination - count : destination;
  memmove(&model[at + count], &model[at],
    (size_t) (modelLines - count - at) * sizeof(uint32_t));
  memcpy(&model[at], moved, (size_t) count * sizeof(uint32_t));
}

NANO_OS_TEST(unit_textstore, lines_added_in_order_fill_whole_blocks) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  for (int ii = 0; ii < 10 * TEXTSTORE_BLOCK_LINES; ii++) {
    NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, ii, 100u + (uint32_t) ii));
    model[modelLines++] = 100u + (uint32_t) ii;
  }

  NANO_OS_ASSERT_EQ_INT(10, store.numBlocks);
  NANO_OS_ASSERT_TRUE(storeMatchesModel(&store));
  textstoreClose(&store);
}

NANO_OS_TEST(unit_textstore, random_edits_match_a_model) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  uint32_t nextOffset = 1;

  for (int step = 0; step < 3000; step++) {
    int choice = randomBelow(10);
    if ((modelLines == 0) || ((choice < 4) && (modelLines < 400))) {
      int after = randomBelow(modelLines + 1);
      int count = 1 + randomBelow(20);
      for (int ii = 0;
        (ii < count) && (modelLines < MODEL_MAX_LINES); ii++, after++
      ) {
        NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, after, nextOffset));
        memmove(&model[after + 1], &model[after],
          (size_t) (modelLines - after) * sizeof(uint32_t));
        model[after] = nextOffset++;
        modelLines++;
      }
    } else if (choice < 6) {
      int first = 1 + randomBelow(modelLines);
      int last = first + randomBelow(modelLines - first + 1);
      if (randomBelow(4) != 0) {
        last = first + randomBelow((modelLines - first < 8)
          ? modelLines - first + 1 : 8);
      }
      textstoreDeleteLines(&store, first, last);
      memmove(&model[first - 1], &model[last],
        (size_t) (modelLines - last) * sizeof(uint32_t));
      modelLines -= last - first + 1;
    } else if (choice < 9) {
      int first = 1 + randomBelow(modelLines);
      int last = first + randomBelow(modelLines - first + 1);
      int destination = randomBelow(modelLines + 1);
      if ((destination >= first) && (destination < last)) {
        destination = (randomBelow(2) == 0) ? first - 1 : last;
      }
      textstoreMoveLines(&store, first, last, destination);
      modelMove(first, last, destination);
    } else {
      int line = 1 + randomBelow(modelLines);
      textstoreSetLineOffset(&store, line, nextOffset);
      model[line - 1] = nextOffset++;
    }
    if (!storeMatchesModel(&store)) {
      fprintf(stderr, "# mismatch after step %d (choice %d)\n", step, choice);
      NANO_OS_ASSERT_TRUE(false);
    }
  }
  textstoreClose(&store);
}

NANO_OS_TEST(unit_textstore, emptied_blocks_are_used_again) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  for (int round = 0; round < 5; round++) {
    for (int ii = 0; ii < 8 * TEXTSTORE_BLOCK_LINES; ii++) {
      NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, ii, (uint32_t) ii));
    }
    textstoreDeleteLines(&store, 1, store.numLines);
    NANO_OS_ASSERT_EQ_INT(0, store.numLines);
    NANO_OS_ASSERT_EQ_INT(TEXTSTORE_NO_BLOCK, store.head);
    NANO_OS_ASSERT_EQ_INT(TEXTSTORE_NO_BLOCK, store.tail);
  }

  NANO_OS_ASSERT_EQ_INT(8, store.numBlocks);
  NANO_OS_ASSERT_EQ_INT(TEXTSTORE_ERROR_NONE, store.error);
  textstoreClose(&store);
}

NANO_OS_TEST(unit_textstore, moving_many_lines_adds_few_blocks) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  for (int ii = 0; ii < 40 * TEXTSTORE_BLOCK_LINES; ii++) {
    NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, ii, (uint32_t) ii));
    model[modelLines++] = (uint32_t) ii;
  }

  textstoreMoveLines(&store, 37, 581, 600);
  modelMove(37, 581, 600);
  NANO_OS_ASSERT_TRUE(storeMatchesModel(&store));
  NANO_OS_ASSERT_TRUE(store.numBlocks <= 43);
  textstoreClose(&store);
}

NANO_OS_TEST(unit_textstore, find_line_returns_the_first_match) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  for (int ii = 0; ii < 50; ii++) {
    NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, ii, 7u * (uint32_t) ii));
  }
  NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, 50, 7u * 33u));

  NANO_OS_ASSERT_EQ_INT(34, textstoreFindLine(&store, 7u * 33u));
  NANO_OS_ASSERT_EQ_INT(1, textstoreFindLine(&store, 0));
  NANO_OS_ASSERT_EQ_INT(0, textstoreFindLine(&store, 5));
  NANO_OS_ASSERT_EQ_INT(TEXTSTORE_ERROR_NONE, store.error);
  textstoreClose(&store);
}

NANO_OS_TEST(unit_textstore, header_data_survives_line_changes) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  uint32_t marks[2] = { 0x12345678u, 0x9abcdef0u };
  NANO_OS_ASSERT_TRUE(textstoreWriteHeader(&store, 100, marks, sizeof(marks)));
  for (int ii = 0; ii < 3 * TEXTSTORE_BLOCK_LINES; ii++) {
    NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, 0, (uint32_t) ii));
  }

  uint32_t read[2] = { 0, 0 };
  NANO_OS_ASSERT_TRUE(textstoreReadHeader(&store, 100, read, sizeof(read)));
  NANO_OS_ASSERT_EQ_INT(0x12345678u, read[0]);
  NANO_OS_ASSERT_EQ_INT(0x9abcdef0u, read[1]);
  textstoreClose(&store);
}

NANO_OS_TEST(unit_textstore, text_round_trips_and_close_removes_both_files) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  FILE *in = tmpfile();
  NANO_OS_ASSERT_NOT_NULL(in);
  fputs("one\ntwo\nthree", in);
  rewind(in);
  long bytesRead = 0;
  NANO_OS_ASSERT_EQ_INT(3, textstoreReadLines(&store, in, 0, false, &bytesRead));
  fclose(in);
  NANO_OS_ASSERT_EQ_INT(13, bytesRead);

  FILE *out = tmpfile();
  NANO_OS_ASSERT_NOT_NULL(out);
  for (int line = 3; line >= 1; line--) {
    NANO_OS_ASSERT_TRUE(textstoreCopyLine(&store, line, out, true) > 0);
  }
  rewind(out);
  char text[32] = { 0 };
  NANO_OS_ASSERT_EQ_INT(14, fread(text, 1, sizeof(text) - 1, out));
  fclose(out);
  NANO_OS_ASSERT_STR_EQ("three\ntwo\none\n", text);

  char blockPath[sizeof(store.path) + 2];
  snprintf(blockPath, sizeof(blockPath), "%s.b", store.path);
  NANO_OS_ASSERT_EQ_INT(0, access(store.path, F_OK));
  NANO_OS_ASSERT_EQ_INT(0, access(blockPath, F_OK));
  textstoreClose(&store);
  NANO_OS_ASSERT_TRUE(access(store.path, F_OK) != 0);
  NANO_OS_ASSERT_TRUE(access(blockPath, F_OK) != 0);
}

NANO_OS_TEST(unit_textstore, reset_removes_every_line) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  for (int ii = 0; ii < 40; ii++) {
    NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, ii, (uint32_t) ii));
  }
  NANO_OS_ASSERT_TRUE(textstoreReset(&store));

  NANO_OS_ASSERT_EQ_INT(0, store.numLines);
  NANO_OS_ASSERT_EQ_INT(0, store.numBlocks);
  NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, 0, 9));
  NANO_OS_ASSERT_EQ_INT(9, textstoreLineOffset(&store, 1));
  textstoreClose(&store);
}

/// @def CHAIN_MAX_RECORDS
///
/// @brief The most records the model of a chain holds.
#define CHAIN_MAX_RECORDS 400

/// @fn static void makeRecord(uint8_t *record, uint32_t value)
///
/// @brief Fill a chain record with bytes that identify a value.
///
/// @param record The TEXTSTORE_CHAIN_RECORD_SIZE bytes to fill.
/// @param value The value.
///
/// @return This function returns no value.
static void makeRecord(uint8_t *record, uint32_t value) {
  for (int ii = 0; ii < TEXTSTORE_CHAIN_RECORD_SIZE; ii++) {
    record[ii] = (uint8_t) (value >> (8 * (ii % 4))) ^ (uint8_t) ii;
  }
}

NANO_OS_TEST(unit_textstore, chains_move_back_and_forward_and_truncate) {
  static uint32_t chainModel[CHAIN_MAX_RECORDS];
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  TextStoreChain chain;
  textstoreChainInit(&chain);
  int numRecords = 0;
  int position = 0;
  uint32_t nextValue = 1;
  uint8_t record[TEXTSTORE_CHAIN_RECORD_SIZE];
  uint8_t expected[TEXTSTORE_CHAIN_RECORD_SIZE];

  for (int step = 0; step < 4000; step++) {
    int choice = randomBelow(10);
    if ((choice < 4) && (position < CHAIN_MAX_RECORDS)) {
      makeRecord(record, nextValue);
      NANO_OS_ASSERT_TRUE(textstoreChainAppend(&store, &chain, record));
      chainModel[position++] = nextValue++;
      numRecords = position;
    } else if (choice < 7) {
      bool moved = textstoreChainPrevious(&store, &chain, record);
      NANO_OS_ASSERT_EQ_INT(position > 0, moved);
      if (moved) {
        makeRecord(expected, chainModel[--position]);
        NANO_OS_ASSERT_TRUE(memcmp(record, expected, sizeof(record)) == 0);
      }
    } else if (choice < 9) {
      bool moved = textstoreChainNext(&store, &chain, record);
      NANO_OS_ASSERT_EQ_INT(position < numRecords, moved);
      if (moved) {
        makeRecord(expected, chainModel[position++]);
        NANO_OS_ASSERT_TRUE(memcmp(record, expected, sizeof(record)) == 0);
      }
    } else {
      // Line edits share the block file and its free list with the chain.
      int after = randomBelow(modelLines + 1);
      NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, after, nextValue));
      memmove(&model[after + 1], &model[after],
        (size_t) (modelLines - after) * sizeof(uint32_t));
      model[after] = nextValue++;
      modelLines++;
      if (randomBelow(3) == 0) {
        int first = 1 + randomBelow(modelLines);
        textstoreDeleteLines(&store, first, first);
        memmove(&model[first - 1], &model[first],
          (size_t) (modelLines - first) * sizeof(uint32_t));
        modelLines--;
      }
    }
    NANO_OS_ASSERT_EQ_INT(TEXTSTORE_ERROR_NONE, store.error);
  }
  NANO_OS_ASSERT_TRUE(storeMatchesModel(&store));
  textstoreClose(&store);
}

NANO_OS_TEST(unit_textstore, appending_after_moving_back_frees_the_rest) {
  TextStore store;
  NANO_OS_ASSERT_TRUE(openStore(&store));
  TextStoreChain chain;
  textstoreChainInit(&chain);
  uint8_t record[TEXTSTORE_CHAIN_RECORD_SIZE];

  for (int round = 0; round < 5; round++) {
    for (int ii = 0; ii < 20 * (int) TEXTSTORE_CHAIN_RECORDS; ii++) {
      makeRecord(record, (uint32_t) ii);
      NANO_OS_ASSERT_TRUE(textstoreChainAppend(&store, &chain, record));
    }
    while (textstoreChainPrevious(&store, &chain, record)) {
    }
  }

  NANO_OS_ASSERT_EQ_INT(20, store.numBlocks);
  NANO_OS_ASSERT_TRUE(textstoreChainFreeAfter(&store, &chain,
    TEXTSTORE_NO_BLOCK));
  NANO_OS_ASSERT_EQ_INT(TEXTSTORE_NO_BLOCK, chain.first);
  for (int ii = 0; ii < 20 * TEXTSTORE_BLOCK_LINES; ii++) {
    NANO_OS_ASSERT_TRUE(textstoreInsertLine(&store, ii, (uint32_t) ii));
  }
  NANO_OS_ASSERT_EQ_INT(20, store.numBlocks);
  textstoreClose(&store);
}

