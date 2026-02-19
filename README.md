# NanoOs

A multitasking nanokernel OS for an Arduino Nano.

## Goals

This work started out as an experiment to see if I could implement an operating system similar to an early version of UNIX in a similar environment.  The first released version of UNIX ran on a PDP-11/20 with 24 KB of RAM about a 1 MHz processor, and about 1.5 MB of disk storage.  Arduinos are the closest modern devices to that kind of environment, so that's where I started.  This code was written for an Arduino Nano 33 IoT, which has 32 KB of RAM, 256 KB of on-board Flash, and a 48 MHz processor.

My first priority was getting a system that would support multiple concurrent processes.  Supporting multiple concurrent users was a non-starter without that ability.  Fortunately, I have a Coroutines library that works in this kind of environment, and I applied it here.

## Environmental Considerations

Stacks for the processes have to be tiny.  In the current implementation, each process has a 720-byte stack configured for it on hardware.  The first version was written for the Arduino Nano Every which has an 8-bit processor.  In that version stacks were only 320 bytes in size.  The increased data width of the stack required a significant increase to the stack size.

The Coroutines library works by segmenting the main stack.  Coroutines are allocated by putting a Coroutine object at the top of each process's stack, so the total size consumed between processes is larger than the size of the stack that's configured.  There's also some additional space allocated due to the way the requested amount of space is broken into chunks.  As of the time of this writing (30-Nov-2025), the stack plus Coroutine object size is 808 bytes when a 720-byte stack is specified.  With this configuration, in 32 KB of RAM and an 8 KB overlay, NanoOs can support 9 processes.  However, some kernel processes are configured to use two conjoined stacks.

## Architecture

NanoOs is a nanokernel architecture.  There is no distinction between user space and kernel space; everything is kernel space.  There's no way to have any kind of memory protection in this kind of environment.

Kernel processes in NanoOs use cooperative multitasking.  The justification for this was two-fold:  (1) It's assumed that only the kernel process knows when it's safe to yield and (2) cooperative multitasking is generally more performant than preemptive multitasking.  User processes are hybrids between cooperative and preemptive.  Strictly speaking, they are designed to be cooperative tasks.  However, on systems where a hardware timer is available, they are preempted if they run too long without yielding.  This prevents a badly-behaving process from hanging the entire system just because it doesn't yield properly.

## Processes

One implication of the nanokernel arhitecture is that there basically is no kernel.  The system is really a collection of processes that communicate with each other in order to accomplish their work.  Each process has a specific task it is responsible for.  When a process needs something from another process, it sends a message to the designated process.  If it needs a response, it blocks waiting for a reply before continuing.

As mentioned, the current implementation supports up to 9 processes.  The metadata in the dynamic memory manager limits the number of concurrent processes to 15.  Support for more than 15 processes would require modification of some constants and data structures within the operating system.

### Kernel Processes

Currently, the following processes are always present and running:

1. The Scheduler - A multi-queue, round-robin scheduler
2. The Console - Polls for user input and displays process output
3. The Memory Manager - Responsible for dynamic memory allocation and deallocation
4. The SD Card - Responsible for low-level access to a MicroSD card
5. The Filesystem - Responsible for providing access to files on a MicroSD card

These processes work together to provide the basic kernel-level functionality.  These processes cannot be killed, even by the root user.

### The Shell and Foreground Processes

NanoOs has a very simple command-line shell.  The shells are not considered special kernel processes.

One of the challenges of having a shell in an embedded OS is consuming a process slot that could be used for other user processes.  To address this, NanoOs borrows a concept from early versions of UNIX.  Commands that are run in the foreground (i.e. are not run with an ampersand at the end of their command lines and are not piping their output to another process) will *REPLACE* the shell process when they execute.  Processes that do not own a console will run in a different process slot.  This allows all foreground processes to be guaranteed a slot to run in.  The shell is automatically restarted by the scheduler when a shell process or a process that replaces one exits.

### User Processes

Currently, all available user processes, including the shell, are implemented in the [src/kernel/Commands.c](Commands.c) library.  However, some of these commands load overlays from the filesystem.  The long-term plan is to have all of the user commands, including the shell, directly implemented as overlays.  This is a work in progress.

### Background Processes

Since NanoOs aims to emulate parts of UNIX, background user processes are supported.  As in UNIX-like environments, launching a process in the background is achieved in a similar manner to UNIX shells:  By appending an ampersand ('&') to the end of the command line.

### Pipes

Pipes of stdout from one process to stdin of another are supported.  Internally, the processes that redirect their stdout are run as background processes.  Only the last process in the chain, which sends its stdout to the console, is run as a foreground process.  Due to the limited amount of memory and the limitation on the number of processes, only three (3) processes may be piped together on one command line.

## Multi-user Support

NanoOs supports two login shells:  One on the serial port available through the USB connection and one on the serial port available through the UART connection.  The shells are started at boot time and are automatically restarted by the scheduler if they exit for any reason.  Like the first version of UNIX, this means that NanoOs can support two (2) concurrent users.

When the shells are started, they are unowned.  When a user logs in, the user takes over ownership of the shell.  Any processes started by the shell become owned by the user that owns the shell.  Only the same user or the root user can kill a process owned by a user.  Only the root user can kill an unowned shell process.

## Message Passing

NanoOs processes are "tasks" built on top of coroutines.  As such, they use the primitives defined in the Coroutines library.  This includes using the library's message passing infrastructure to send and receive messages among them.  All kernel processes have well-known process IDs and handle incoming messages at least once for every iteration of their main loop.

When a user process needs something from one of the kernel processes, it prepares and sends a command message to the process.  If the command is asynchronous, the user process can immediately return to its other work.  If the command is synchronous, the user process can block waiting for a response from the kernel process.  All system operations are handled this way.

## Dynamic Memory

NanoOs does support dynamic memory, just not very much of it.  The memory manager is started as the last process and makes use of all of the remaining memory at that time.  As of 2-Feb-2026, that amount is about 6 KB.

The memory manager is a first-fit allocator that supports automatic memory compaction and is O(1) in the LIFO case.  Any time the last pointer in the allocation list is freed, the next pointer is moved backward until a block that has not been freed is found or until the beginning of dynamic memory is reached.  When a block of memory is freed that is next to another piece of freed memory, the blocks are joined together into a contiguous block of free memory.

The memory manager also tracks the size of each allocation.  This allows for realloc to function correctly.  If the size of the reallocation is less than or equal to the size already allocated, no action is taken.  If the size of the reallocation is greater than the size already allocated, the old memory can be copied to the new location before returning the new pointer to the user (as is supposed to happen for realloc).

The owning process of a piece of dynamically-allocated memory is also tracked.  By default, this is the process that originally made the allocation request.  However, the scheduler has the ability to reassign ownership on process creation.  This allows for all memory owned by an individual process to be freed in the event it is prematurely terminated.

## Filesystem

NanoOs uses a custom exFAT implementation.  The filesystem is a dedicated process that other processes exchange messages with.  The filesystem, in turn, exchanges messages with the SD card process to perform IO on the card.

## Other Branches

For the codebase with a FAT16 filesystem on an SD card and user processes that are compiled into the binary, see the [embedded branch](https://github.com/james-card/NanoOs/tree/embedded).  For the codebase without a filesystem and user processes that are compiled into the binary, see the [embedded branch with no filesystem](https://github.com/james-card/NanoOs/tree/embedded-no-filesystem).  For the codebase with user processes that are run from a filesystem on an SD card in an RV32I VM, see the [rv32i-vm branch](https://github.com/james-card/NanoOs/tree/rv32i-vm).  **All of these branches are considered deprecated and incomplete.**

## Development History

The development history of this work is archived at this repository's [GitHub Pages](https://james-card.github.io/NanoOs/) site.

