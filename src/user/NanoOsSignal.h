///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              03.08.2026
///
/// @file              NanoOsSignal.h
///
/// @brief             Kernel signal number definitions exposed to userspace.
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

#ifndef NANO_OS_USER_SIGNALS_H
#define NANO_OS_USER_SIGNALS_H

#include "NanoOsLimits.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Numbering layout:
//   1–19   POSIX.1-2017 base signals
//  20–29   XSI extension signals (SUS/Single Unix Specification)
//  30+     POSIX.1-2001 real-time signals
//
// POSIX does not standardise numeric values — only the names. These
// assignments are NanoOS-specific and do not match any host ABI.
//
// References:
//   POSIX.1-2017  <signal.h>  (base standard)
//   XSI extension             (marked [XSI] below)
//   POSIX.1-2001              (real-time signals)

// -----------------------------------------------------------------------
// POSIX.1-2017 base signals (1–19)
// -----------------------------------------------------------------------

#define SIGHUP      1   // Hangup: controlling terminal closed or
                        //   controlling process died
#define SIGINT      2   // Interrupt: interactive attention request (Ctrl-C)
#define SIGQUIT     3   // Quit: like SIGINT but produces a core dump
#define SIGILL      4   // Illegal instruction
#define SIGABRT     5   // Abort: sent by abort(3)
#define SIGFPE      6   // Arithmetic exception (integer or floating-point)
#define SIGKILL     7   // Kill: cannot be caught, blocked, or ignored
#define SIGUSR1     8   // User-defined signal 1
#define SIGSEGV     9   // Segmentation violation: invalid memory reference
#define SIGUSR2    10   // User-defined signal 2
#define SIGPIPE    11   // Broken pipe: write to pipe with no readers
#define SIGALRM    12   // Alarm: sent by alarm(2) when the timer expires
#define SIGTERM    13   // Termination: polite shutdown request
#define SIGCHLD    14   // Child process stopped or terminated
#define SIGCONT    15   // Continue execution if currently stopped
#define SIGSTOP    16   // Stop: cannot be caught, blocked, or ignored
#define SIGTSTP    17   // Terminal stop: interactive stop request (Ctrl-Z)
#define SIGTTIN    18   // Background process attempted read from terminal
#define SIGTTOU    19   // Background process attempted write to terminal

// -----------------------------------------------------------------------
// XSI extension signals (20–29)
// Required by the X/Open System Interfaces extension to POSIX (SUS).
// -----------------------------------------------------------------------

#define SIGTRAP    20   // [XSI] Trace/breakpoint trap
#define SIGBUS     21   // [XSI] Bus error: misaligned or nonexistent address
#define SIGURG     22   // [XSI] High-bandwidth data available on socket
#define SIGXCPU    23   // [XSI] CPU time limit exceeded (see setrlimit(2))
#define SIGXFSZ    24   // [XSI] File size limit exceeded
#define SIGVTALRM  25   // [XSI] Virtual timer expired (ITIMER_VIRTUAL)
#define SIGPROF    26   // [XSI] Profiling timer expired (ITIMER_PROF)
#define SIGWINCH   27   // [XSI] Terminal window size changed
#define SIGPOLL    28   // [XSI] Pollable event (I/O possible on a descriptor)
#define SIGSYS     29   // [XSI] Bad system call: invalid argument to syscall

// -----------------------------------------------------------------------
// POSIX.1-2001 real-time signals (30+)
//
// POSIX requires at least _POSIX_RTSIG_MAX (8) distinct RT signals.
// RT signals are queued rather than coalesced, carry a sigval payload
// via sigqueue(3), and are delivered lowest-number-first within the
// same scheduling priority.
//
// Always use SIGRTMIN+n offsets in user code — never hardcode the
// underlying numbers, as an implementation may reserve the lowest
// RT signal numbers for internal use.
// -----------------------------------------------------------------------

#define SIGRTMIN   30
#define SIGRTMAX   (SIGRTMIN + _POSIX_RTSIG_MAX - 1)   // minimum range

#define NSIGRT     (SIGRTMAX - SIGRTMIN + 1)

// -----------------------------------------------------------------------
// Handler pseudo-values (POSIX.1-2017 §<signal.h>)
// -----------------------------------------------------------------------

typedef void (*sighandler_t)(int);

#define SIG_DFL    ((sighandler_t)0)    // Default action for signal
#define SIG_IGN    ((sighandler_t)1)    // Ignore signal
#define SIG_ERR    ((sighandler_t)-1)   // Error return from signal(2)

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_SIGNALS_H

