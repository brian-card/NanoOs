///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_console.c
///
/// @brief             Unit tests (no kernel boot) for consoleProcessByte, the
///                    console's per-byte line editor.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Console.h"

/// @struct TestConsole
///
/// @brief A ConsolePort with its own ConsoleBuffer, as runConsole sets one up.
typedef struct TestConsole {
  ConsolePort   port;
  ConsoleBuffer buffer;
} TestConsole;

/// @fn static void testConsoleInit(TestConsole *testConsole)
///
/// @brief Give a TestConsole an empty line and the normal editing state.
///
/// @param testConsole A pointer to the TestConsole to initialize.
///
/// @return This function returns no value.
static void testConsoleInit(TestConsole *testConsole) {
  memset(testConsole, 0, sizeof(*testConsole));
  testConsole->port.consoleBuffer = &testConsole->buffer;
  testConsole->port.inputState = CONSOLE_INPUT_STATE_NORMAL;
}

/// @fn static const char* testConsoleLine(TestConsole *testConsole)
///
/// @brief NUL-terminate and return the line edited so far.
///
/// @param testConsole A pointer to the TestConsole to read.
///
/// @return Returns a pointer to the line.
static const char* testConsoleLine(TestConsole *testConsole) {
  testConsole->buffer.buffer[testConsole->port.consoleBufferIndex] = '\0';
  return testConsole->buffer.buffer;
}

NANO_OS_TEST(unit_console, printable_bytes_are_stored_and_echoed) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, 'l', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_CHAR, echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, 's', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_CHAR, echo);
  NANO_OS_ASSERT_STR_EQ("ls", testConsoleLine(&tc));
}

NANO_OS_TEST(unit_console, return_completes_a_line_ending_in_newline) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  consoleProcessByte(&tc.port, 'l', &echo);
  consoleProcessByte(&tc.port, 's', &echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_LINE,
    consoleProcessByte(&tc.port, '\r', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NEWLINE, echo);
  NANO_OS_ASSERT_STR_EQ("ls\n", testConsoleLine(&tc));
}

NANO_OS_TEST(unit_console, line_feed_after_return_is_swallowed) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_LINE,
    consoleProcessByte(&tc.port, '\r', &echo));
  tc.port.consoleBufferIndex = 0; // as the line's dispatch does

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, '\n', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NONE, echo);
  NANO_OS_ASSERT_EQ_INT(0, tc.port.consoleBufferIndex);
}

NANO_OS_TEST(unit_console, byte_after_return_starts_the_next_line) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  consoleProcessByte(&tc.port, '\r', &echo);
  tc.port.consoleBufferIndex = 0; // as the line's dispatch does

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, 'p', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_CHAR, echo);
  NANO_OS_ASSERT_STR_EQ("p", testConsoleLine(&tc));
}

NANO_OS_TEST(unit_console, line_feed_alone_completes_a_line) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  consoleProcessByte(&tc.port, 'x', &echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_LINE,
    consoleProcessByte(&tc.port, '\n', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NEWLINE, echo);
  NANO_OS_ASSERT_STR_EQ("x\n", testConsoleLine(&tc));
}

NANO_OS_TEST(unit_console, backspace_on_an_empty_line_does_nothing) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_CHAR;

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, ASCII_BACKSPACE, &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NONE, echo);
  NANO_OS_ASSERT_EQ_INT(0, tc.port.consoleBufferIndex);
}

NANO_OS_TEST(unit_console, backspace_and_delete_erase_the_last_byte) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  consoleProcessByte(&tc.port, 'a', &echo);
  consoleProcessByte(&tc.port, 'b', &echo);
  consoleProcessByte(&tc.port, 'c', &echo);
  consoleProcessByte(&tc.port, ASCII_BACKSPACE, &echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_ERASE, echo);
  consoleProcessByte(&tc.port, ASCII_DELETE, &echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_ERASE, echo);
  NANO_OS_ASSERT_STR_EQ("a", testConsoleLine(&tc));
}

NANO_OS_TEST(unit_console, full_line_drops_bytes_without_echoing_them) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  for (int ii = 0; ii < CONSOLE_BUFFER_SIZE - 1; ii++) {
    consoleProcessByte(&tc.port, 'z', &echo);
  }
  NANO_OS_ASSERT_EQ_INT(CONSOLE_BUFFER_SIZE - 1, tc.port.consoleBufferIndex);

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, 'y', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NONE, echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_BUFFER_SIZE - 1, tc.port.consoleBufferIndex);
}

NANO_OS_TEST(unit_console, csi_sequence_completes_on_its_final_byte) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, ASCII_ESCAPE, &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, '[', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, '1', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_ESCAPE,
    consoleProcessByte(&tc.port, '~', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NONE, echo);
  NANO_OS_ASSERT_STR_EQ("\x1b[1~", testConsoleLine(&tc));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_STATE_NORMAL, tc.port.inputState);
}

NANO_OS_TEST(unit_console, ss3_sequence_completes_after_one_more_byte) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  consoleProcessByte(&tc.port, ASCII_ESCAPE, &echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(&tc.port, 'O', &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_ESCAPE,
    consoleProcessByte(&tc.port, 'P', &echo));
  NANO_OS_ASSERT_STR_EQ("\x1bOP", testConsoleLine(&tc));
}

NANO_OS_TEST(unit_console, escape_plus_one_other_byte_completes) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_NONE;

  consoleProcessByte(&tc.port, ASCII_ESCAPE, &echo);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_ESCAPE,
    consoleProcessByte(&tc.port, 'x', &echo));
  NANO_OS_ASSERT_STR_EQ("\x1bx", testConsoleLine(&tc));
}

NANO_OS_TEST(unit_console, ctrl_c_is_an_interrupt_and_is_not_stored) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_CHAR;

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_INTERRUPT,
    consoleProcessByte(&tc.port, 0x03, &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NONE, echo);
  NANO_OS_ASSERT_EQ_INT(0, tc.port.consoleBufferIndex);
}

NANO_OS_TEST(unit_console, other_control_bytes_are_unhandled) {
  TestConsole tc;
  testConsoleInit(&tc);
  ConsoleEcho echo = CONSOLE_ECHO_CHAR;

  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_UNHANDLED,
    consoleProcessByte(&tc.port, 0x01, &echo));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_ECHO_NONE, echo);
  NANO_OS_ASSERT_EQ_INT(0, tc.port.consoleBufferIndex);
}
