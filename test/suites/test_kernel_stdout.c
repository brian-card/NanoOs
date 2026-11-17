///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_stdout.c
///
/// @brief             Tests for the buffering of output written to stdout:
///                    what is held, and what sends it to the console.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/NanoOs.h"
#include "kernel/Console.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

#include "HalMock.h"

extern FILE *nanoOsStdout;
extern FILE *nanoOsStderr;
size_t nanoOsFwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
void nanoOsCloseStdout(void);

/// @fn static size_t drainConsole(char *out, size_t size)
///
/// @brief Let the console write what it has been sent, then take what it
/// wrote to the UART.
///
/// @param out The buffer to put the output in, NUL-terminated.
/// @param size The size of out.
///
/// @return Returns the number of bytes taken.
static size_t drainConsole(char *out, size_t size) {
  for (int ii = 0; ii < 8; ii++) {
    processYield();
  }
  size_t length = mockUartDrain(out, size - 1);
  out[length] = '\0';
  return length;
}

/// @var testFileDescriptors
///
/// @brief The test process's stdin, stdout, and stderr:  output goes to the
/// console.
static FileDescriptor testFileDescriptors[3];

/// @var testFileDescriptorPointers
///
/// @brief The test process's file descriptor table.
static FileDescriptor *testFileDescriptorPointers[3];

/// @fn static void stdioFixture(void)
///
/// @brief Give this process a stdout and stderr that write to the console.
///
/// @return This function returns no value.
static void stdioFixture(void) {
  memset(testFileDescriptors, 0, sizeof(testFileDescriptors));
  for (int ii = 0; ii < 3; ii++) {
    testFileDescriptors[ii].inputChannel.pid = PROCESS_ID_NOT_SET;
    testFileDescriptors[ii].outputChannel.pid
      = (ii == 0) ? PROCESS_ID_NOT_SET : consolePid;
    testFileDescriptors[ii].outputChannel.messageType = CONSOLE_WRITE_BUFFER;
    testFileDescriptors[ii].refCount = 1;
    testFileDescriptorPointers[ii] = &testFileDescriptors[ii];
  }
  getRunningProcess()->fileDescriptors = testFileDescriptorPointers;
  getRunningProcess()->numFileDescriptors = 3;
}

/// @fn static void discardConsole(void)
///
/// @brief Throw away everything written to the UART so far.
///
/// @return This function returns no value.
static void discardConsole(void) {
  char scratch[256];
  stdioFixture();
  while (drainConsole(scratch, sizeof(scratch)) > 0) {
  }
}

NANO_OS_KERNEL_TEST(stdout, writes_are_held_until_a_newline) {
  char out[256];
  discardConsole();

  nanoOsFwrite("ab", 1, 2, nanoOsStdout);
  nanoOsFwrite("cd", 1, 2, nanoOsStdout);
  NANO_OS_ASSERT_EQ_INT(0, (long long) drainConsole(out, sizeof(out)));
  NANO_OS_ASSERT_NOT_NULL(getProcessStorage(STDOUT_BUFFER_KEY));

  nanoOsFwrite("e\nf", 1, 3, nanoOsStdout);
  drainConsole(out, sizeof(out));
  NANO_OS_ASSERT_STR_EQ("abcde\r\nf", out);
  nanoOsCloseStdout();
}

NANO_OS_KERNEL_TEST(stdout, closing_sends_what_is_held_and_frees_it) {
  char out[256];
  discardConsole();

  nanoOsFwrite("held", 1, 4, nanoOsStdout);
  nanoOsCloseStdout();
  drainConsole(out, sizeof(out));
  NANO_OS_ASSERT_STR_EQ("held", out);
  NANO_OS_ASSERT_NULL(getProcessStorage(STDOUT_BUFFER_KEY));
}

NANO_OS_KERNEL_TEST(stdout, stderr_follows_what_stdout_holds) {
  char out[256];
  discardConsole();

  nanoOsFwrite("out ", 1, 4, nanoOsStdout);
  nanoOsFwrite("err", 1, 3, nanoOsStderr);
  drainConsole(out, sizeof(out));
  NANO_OS_ASSERT_STR_EQ("out err", out);
  nanoOsCloseStdout();
}

NANO_OS_KERNEL_TEST(stdout, a_full_buffer_is_sent_before_more_is_held) {
  char out[512];
  char line[60];
  discardConsole();
  memset(line, 'x', sizeof(line));

  nanoOsFwrite(line, 1, sizeof(line), nanoOsStdout);
  nanoOsFwrite(line, 1, sizeof(line), nanoOsStdout);
  NANO_OS_ASSERT_EQ_INT(sizeof(line), (long long) drainConsole(out, sizeof(out)));
  nanoOsCloseStdout();
  NANO_OS_ASSERT_EQ_INT(sizeof(line), (long long) drainConsole(out, sizeof(out)));
}

ConsoleBuffer* nanoOsWaitForInput(void);

/// @fn static ProcessMessage* queueInputFrom(ProcessId pid,
///   ConsoleBuffer *consoleBuffer)
///
/// @brief Put input on this process's queue as if process pid had sent it.
///
/// @param pid The process the input is from.
/// @param consoleBuffer The buffer the input is in.
///
/// @return Returns the message queued, or NULL if there was none.
static ProcessMessage* queueInputFrom(ProcessId pid,
  ConsoleBuffer *consoleBuffer
) {
  ProcessMessage *processMessage = getAvailableMessage();
  if (processMessage == NULL) {
    return NULL;
  }
  processMessageInit(processMessage,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_RETURNING_INPUT,
    consoleBuffer, sizeof(*consoleBuffer), false);
  processMessageQueuePush(getRunningProcess(), processMessage);
  msg_from(processMessage).coro = allProcesses[pid - 1].mainThread;
  return processMessage;
}

NANO_OS_KERNEL_TEST(stdin, input_from_a_writer_stdin_no_longer_reads_is_dropped) {
  static ConsoleBuffer strayInput;
  static ConsoleBuffer writerInput;
  ProcessId strayPid = getRunningPid() + 1;
  ProcessId writerPid = getRunningPid() + 2;
  // A sender with no process context counts as the kernel, which is accepted.
  NANO_OS_ASSERT_NOT_NULL(
    coroutineContext(allProcesses[strayPid - 1].mainThread));
  NANO_OS_ASSERT_NOT_NULL(
    coroutineContext(allProcesses[writerPid - 1].mainThread));
  stdioFixture();
  testFileDescriptors[0].inputChannel.pid = writerPid;
  testFileDescriptors[0].inputChannel.messageType = CONSOLE_RETURNING_INPUT;

  NANO_OS_ASSERT_NOT_NULL(queueInputFrom(strayPid, &strayInput));
  NANO_OS_ASSERT_NOT_NULL(queueInputFrom(writerPid, &writerInput));

  NANO_OS_ASSERT_EQ_PTR(&writerInput, nanoOsWaitForInput());
  NANO_OS_ASSERT_NULL(processMessageQueuePop());
}
