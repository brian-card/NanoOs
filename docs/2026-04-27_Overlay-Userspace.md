# 27-Apr-2026 - Overlay Userspace

"Where do we... begin...?"
[*- The Joker* from **The Dark Knight**](https://www.youtube.com/watch?v=L2n68Inpo34)

This effort consumed over three months of my nights and weekends.  I wrote a search algorithm a few years ago and that took me six months.  That's the only effort that's ever consumed more time on one of my personal projects.

I suppose I should start out talking about what the goal was.  Simply put, I wanted an expandable system.  Prior to this effort, adding a new command required modifying the operating system code.  Now, it's a matter of adding a new set of files on the filesystem.

Now, as previously mentioned in my [Getting Closer](2025-10-26_Getting-Closer.md) post, I had already worked out the basic mechanics of how overlays could work and had some prototypes working.  For this effort, though, I wanted to move **ALL** of the existing built-in commands and the shell into overlays while still maintaining functional parity with what I had previously developed in the built-in versions.

That by itself wasn't good enough, though.  The overall goal of this entire project is to produce a Unix-like system.  For that, the overlay commands didn't just need to be loaded from the filesystem, they needed to use Unix system calls.  THAT was an issue.  The commands that I had built into the kernel were kind of half-Unix.  Some things used standard C or Unix calls and some things didn't.  For this effort, I needed to implement the proper Unix flows any time there was one and all the backing system calls to support them.

As always, I did research on how other implementations do things.  I was pretty disappointed by what I found.  Some things were straightforward.  For the `kill` command there's a `kill()` system call.  Some things, however, were very much not.

For instance, I discovered that the Unix specifcation mandates that a ps command exist, but not how it accomplishes its work.  On Linux, the command traverses the `/proc` directory.  Other systems, however, use implementation-specific system calls.  That was unexpected.  That put me in the position of having to expose the same infrastructure I used in the embedded commands into userspace.

Another "gotcha" was how to use overlays from userspace.  I don't think overlays have been part of an official release of Unix in over 40 years.  Even when they were present, they were an implementation detail.  There was never anything about them in "Unix" itself.  So, this was another area where I had to expose a custom API to userspace.

I'm going to skip all the details of implementing all the system calls, but there were a lot.  This is, of course, nowhere close to everything, it's just the set of calls that brings me to being on-par with what I already had.

I ran into a pretty big problem when I started running multiple overlay-based commands in parallel:  Performance.  When multiple commands were being swapped out of the overlay space, things slowed to a crawl.  I realized the problem was an implementation detail.  Overlays can only be loaded by the scheduler, not by userspace programs.  I had been keeping track of what overlay to load by a filename pointer in the task descriptor.  The implication of that was that, in order to load an overlay into the space:

- Memory had to be allocated for the FILE object
- The directory on disk had to be scanned for the metadata of the file
- The metadata had to be read into memory
- The contents of the file had to be read into a block buffer
- The memory then had to be copied to the overlay
- The file had to be closed and all memory freed

And all that had to happen on every single swap.  This was obviously horribly inefficient.

It occurred to me that all I really needed to know was the file's starting block and the number of blocks it consumed.  The compiler guarantees that overlays will fit into memory, so I didn't need to do any bounds checking.  What I really needed was a way to read the files start block and number of blocks once and then a way to just read blocks straight from the SD card into memory.

None of that infrastructure existed, of course, so I had to invent it all.  One complication in this is that block devices are implemented as processes, not libraries.  So, the scheduler can't directly call a library function to do the read.  It has to prepare a message to send to the SD card process and run through things until the process finishes doing the load.  That was frustrating.  I briefly considered turning the SD card into a HAL layer construct before deciding against that.  That would really make things a **LOT** more monolithic and I didn't like that.  So, I bit the bullet and implemented proper process-level abstractions.

Another big hurdle was figuring out how to manage pipes from the shell.  Normally, the shell would `fork()` a bunch of copies, each of which would `exec()` part of the pipeline.  That's not possible in NanoOs at the moment because I don't have a way to `fork()`.  Fortunately, there's an alternative:  `posix_spawn()`.  Of course, going that route came with its own set of dedicated system calls, so I had to implement those as well, but at least there was a clean "Unix" way of doing things for that one.

An, Oh! the bugs that got created when I tried to implement pipes for the first time were something to behold!!  I was pretty tired when I was wrapping up the implementation and inadvertently created a situation where a process could send its output to the scheduler.  The scheduler, of course, had no idea what to do with that and silently threw it away.  That, in turn, resulted in the message that had been sent being reused by another process while the original was still waiting on a response.  What a mess that was!!  It took me the majority of a day to track down and fix that one!

Still, in the end, I succeeded.  Now, unfortunately, I'm running out of memory when trying to pipe more then two commands together and may be corrupting the stack in the process, but that's an issue to work out differently.  There were a **LOT** of things that I uncovered that need to be fixed before going any further.  One of those things is the legacy messaging system.  It's currently consuming too much RAM and not providing enough messages to the processes, so fixing that will go a long way toward fixing some of the memory issues.  There's a laundry list of other things too but I'll cover those in some future post.

For now, I'm pleased that after all this time, I finally have a way to do multitasking with userspace processes that run from the filesystem instead of being baked into the kernel.  This paves the way for a more flexible and feature-rich OS.

To be continued...

[Table of Contents](.)
