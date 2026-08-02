# 2-Aug-2026 - Saving Space

OK... I suppose I should first admit that if I was the technical lead of this effort as a commerical product, I would probably have a talk with me about the code I just pushed.  That, or I would just outright reject it and tell me to do it right.  I plead guilty to not following engineering best practices regarding proper scoping of feature branches.

Now that that's out of the way, I can excitedly claim great success in my efforts to reduce codespace!  **HOORAY!!!**

In my earlier post about the [overlay-based filesystem](2026-06-20_Overlay-Filesystem.md), I mentioned two ideas I had for reducing the size of the OS binary: stripping the strings from the binary and converting the filesystem process to an overlay process.  That post explained the challenges with running the filesystem as an overlay and why I came to the conclusion that that wasn't realistic due to the performance of it.  So, my most-recent effort was supposed to be focused on the other idea of stripping the strings from the binary.

Here's the approach I was taking to this effort:

1. Create log macros for logging messages out of Kernel- and Executive-level processes
2. Convert all the existing `printString` calls to their equivalent log macro calls in the existing code
3. Protect any strings not in log calls and any other data that might wind up in the .rodata section from landing in that section
 - This means that the .rodata section of the final .elf would contain only strings from log macro calls
4. Make the log macros call a function that would create a message containing the offsets of the strings used instead of the raw strings
5. Create a logger process that could read the strings to be used from the extracted .rodata section to produce the intended log message on UART0

The first step was pretty simple and straightforward, so I won't cover it here.

The second step was a giant pain in the neck.  I used Claude to automate the process of combining the existing `printString`, `printInt`, and `printHex` calls into coheret, one-line calls that used the new macros.  On the plus side, it did convert combinations of calls into `printf`-style calls.  On the down side, it did absolutely zero optimization of removing things like "ERROR" from the strings of error messages when calling the new `logError` macro, so there was then a whole bunch of redundant information in the code.  It also used `logError` for everything instead of using things like `logWarning` when appropriate.  I wound up having to go through each and every log message and convert it to the correct macro and remove redundant information.

I used Claude for step 3 as well and it did so-so.  To its credit, it did save me a lot of time hoisting strings into protected global variables.  On the down side, it missed several places it should have protected, despite me telling it explicitly what to check multiple times.  I ran into several segfaults when I started testing with what should have been a protected binary because it had failed to properly protect things from going to .rodata.  One of the more notable ones was, despite me telling it to change the makefiles to not use jump tables (that would have gone to .rodata), it failed to fix all of the makefiles initially, so I wound up with crashes in weird places.  I would like to thank the makers of Valgrind for helping me track down some of those problems.  **THANK YOU!!!**

The logger process took some work on multiple fronts.  First off, we're trying to conserve flash space here, so this obviously had to be an overlay process, not one baked into the OS image.  However, this process has to be able to directly access things in the HAL.  The HAL is only accessible to Kernel and Executive processes, so this obviously had to be an executive process.  The problem, though, once I got it coded, was getting it started.  Because of all the recent work I did with [capabilities](2026-07-05_Scalability.md), I had to add both new capabilities for the existing system processes to talk to the logger when logging a message and for the logger process to talk to the filesystem and memory manager for doing its string work.  On top of that, there were order-of-operations issues to consider here.  Processes have to come up and exchange initialization information with each other in a certain order.  I had to take care to not disturb that dance when adding a new process to the mix.

And let's talk about that work for a second.  Starting the OS happens in phases.  The bootloader calls `nanoOsStart`, which does some initial configuration of threads before calling `startScheduler`.  `startScheduler` configures and kicks off all the processes in the system and then goes into an infinite-loop calling `runScheduler` with different process queues in effect.  As probably surprises no one, `startScheduler` has grown organically over time to involve more and more operations over time.  With the addition of yet another process in the sequence, I had to spend some time straightening some things out.  I broke it down into four (4) sub-functions.  I'm still not thrilled with the complexity of each one of them, but they're a heck of a lot better than having that huge mess in one function, so I'll take that as a win.

Somewhere in step 2 or 3, I started making builds with the .rodata section stripped from them and the .rodata section as a separate artifact.  Initially, the sizse of .rodata was about 11.5 KB.  By the time I got done protecting things, it was about 10.5 KB.  Now, my goal is to port NanoOs to the AgonLight2, which uses the eZ80 processor.  Prior to any size reduction work, my build was consuming 130088 of the 131072 available bytes of flash storage on that chip, leaving me a whopping 984 bytes to implement a HAL (which is currently just stubbed out for that target).  I realized that increasing that by only 11.5 KB was cutting things dangerously close, especially given how much code space 32-bit math seems to take up on this platform.  I figured I needed to find something more drastic if I could.

I played around with the setting the filesystem back to overlay, since that path eliminated all of the built-in filesystem logic.  That produced a binary about 112 KB in size.  That meant that the same logic that consumed about 6 KB on the Cortex M0 consumed about 17.5 KB on the eZ80.  Ah, the joys of implementing 32-bit math with 24-bit registers...  Having an additional 17.5 KB of flash space was definitely worth it, so I started trying to figure out what I could do here.  Hence why this effort grew beyond its original scope.

Now, I knew from my previous efforts trying to run the filesystem as an overlay that, (a) yes it was possible and (b) swapping the filesystem in and out of memory resulted in unacceptable performance.  That was on a 32-bit processor running at 48 MHz, too.  The eZ80 in the AgonLight2 is an 8-bit processor running at about 18 MHz, so that strategy would make the system pretty much unusable in this environment.

However, I figured if I could have the entire filesystem code as a contiguous block of memory, it should be no different in performance than having it in a contiguous block of flash.  In fact, on the AgonLight2, it might even be faster because RAM on that platform is zero wait-state.  Reads out of flash likely take more cycles to complete than readss out of RAM.  Having it as a contiguous block of RAM seemed doable, so I set down that road.

In principle, this was pretty straightforward.  I had already solved most of the complicating factors by creating the overlay-based system.  It's simpler than the overlay strategy because I didn't have to worry about loading the next overlay, I could just call the linked-in function directly.  Loading it was also simpler because all I had to do was just load a fixed number of blocks from the block device at boot and kick off the process.  All the logic I worked out for starting the overlay-based filesystem could be reused.

Well, almost.  There was one complicating factor.  All of the existing overlay code assumes that there is only one overlay space in the system.  For this, I couldn't use that address.  For one thing, the amount of code I needed to load was much larger than that space.  For another, in order for this to even make sense at all, the memory for the filesystem has to be physically separate from that space.  The implication of that is that I couldn't use the existing load and start mechanisms.  I had to load the code from disk into memory and kick off the main function all manually.  Not a huge deal, just something I had to take care of.

Since I don't have a functional HAL for the AgonLight2 yet, I couldn't try this approach on a real system.  But, I could try it on the simulator, so that's what I did.  To my absolute shock and delight, it worked correctly on the first try!  I guess that really shouldn't have surprised me that much given that most of the work was already done in the overlay effort, but it was definitely pleasing.

With all that done and things put back together again, my OS image size on the AgonLight2 dropped from 130,088 bytes to 101,400 bytes for a total savings of 28,688 bytes.  Nice!!!  That should be more than enough to implement a HAL and get things going.

One consequence of adding the logger was that I added in support for a 10th process.  The alternative was sacrificing one of the existing user processes and I wasn't willing to do that.  That means there's a little less RAM available in the system now (about 1 KB less on my SAMD21-based Arduino), but I'mOK with that.

So, with now almost 30 KB of available space, it's time to start work on a HAL for this thing and see what I can do.  This will be the first time I'll be implementing a HAL from complete scratch (i.e. not relying on any vendor libraries), so this should be pretty entertaining.  I can't wait to get started!!!

To be continued...

[Table of Contents](.)
