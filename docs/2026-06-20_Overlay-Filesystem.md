# 20-Jun-2026 - Overlay Filesystem

\*sigh\*  Well, I tried...

In preparing to port the OS to the eZ80, I'm continuing to look for ways to conserve space.  Prior to this effort, I had two remaining things to try to do that:  Stripping the strings from the binary and converting the filesystem into an overlay process.  I had previously done some messing around with removing the strings in the binary and knew that that would save about 11 KB.  I expected the filesystem logic to be larger than that, so I started with turning it into an overlay process first.

Conceptually, there really wasn't much to this.  You turn the filesystem message handlers into one overlay each and load them via the block overlay path.  I had already put in a block-based overlay function a few weeks ago, so really all that remained was to do the conversion.  That was essentially a copy-and-paste exercise in principle.  In practice, it was a little more involved than that because I needed to organize the code in such a way that I could implement a different filesystem at some future time if I so chose, but not much more complicated.

What turned out to be a pain was making the changes to the scheduler and the HALs so that the filesystem could be loaded and run as an overlay process.  (Side note:  Starting the filesystem process is currently a HAL function because I had the idea at one point that not all hardware would want a filesystem implementation.  I may change that in the near future, though.)  The big issue was that overlays can't run until scheduler initialization completes.  Prior to this change, starting the filesystem happened relatively early in scheduler init.  That was possible because the filesystem was a built-in process.  With it moving out into an overlay, the whole sequence had to change.

What I eventually did was add a new member to the ProcessDescriptor that held a reset function pointer that would be called if a process exited.  This allowed me to create the filesystem process with a dummy function initally and then have the scheduler "restart" it (which would actually be its initial launch) when the dummy process exited.  This also simplified things for the existing "shell" processes.  With this in place, I didn't have to do any special accommodations for shell processes any longer.  I just made a restart function that handled the logic.  I changed the scheduler to call the restart function on all processes except for "user" processes that are spawned in non-shell process slots.

After that and loading the new overlays into their blocks, it was time to give it a shot.  It crashed, of course.  I was still doing development in the simulator at that point, so I ran it through valgrind.  It complained about hitting an illegal instruction.  Well, that made things obvious immediately.  Clearly the wrong overlay was in memory when it tried to do something.  I enabled debugging in the kernel and, to my surprise, discovered that the filesystem was actually starting and running just fine.  It was crashing when trying to bring in getty.  This made things pretty obvious:  The filesystem was in memory when the user process was being resumed.

The reason for this was also obvious.  I had a function called `runKernelExecutive` that I called in a few places when I needed to run a few cycles of those to privilege levels so that something would be processed.  One of those places was when reading in blocks from the SD card.  There was never an issue with this procedure when the filesystem was built-in, but with it being an overlay, it was being loaded and overwriting the contents of the user process while the overlay from the user process was being read in.  Since the filesystem had been downgraded to an "executive" level in the course of this work, the solution was to just run "kernel" level processes (which include the SD card process) until the read was complete instead of both kernel and executive processes.  I changed the function to `runSchedulerQueues` which took a parameter to bound the queues that got run and just bypassed running the filesystem.

With that in place, everything worked great!  Zero difference in functionality!  Two questions then remained:  What was the impact to performance on real hardware (it was impossible to tell on the simulator) and what was the impact to OS binary size?

I didn't get around to checking on those two things until the next day.  What I found did not amuse me.  I had hoped for a binary savings of 12 KB+.  Actual savings was right at 6 KB.  On a 96 KB original binary size, that was only a savings of about 6%.  But the real kicker was the performance impact.  Commands took **3X** as long to run!!  So that 6% binary savings cost an extra 200% in execution time!!!

This is, simply put, completely unacceptable.  As much as I need to conserve space, the performance hit basically makes the system unusable.  It's bad on the SAMD21-based Arduino I'm using.  I expect it to be much worse on the eZ80 since I'd be going from a 48 MHz 32-bit system to an 18 MHz 8-bit system.  And it's *DEFINITELY* not worth it for a measly 6 KB.

So, this was a bit of an exercise in futility in terms of achieving the stated goal, but it was not a complete waste of time.  Both the scheduler logic and the HAL logic became simpler during this work, so that's something.  And, it did prove out that block-based overlays are completely possible, so that's positive.  And, as always, I learned and that's the ultimate goal of this entire effort.

So, I won't be using an overlay-based filesystem.  I'll move on to stripping the string out of the binary next.  We'll see how that works out.

To be continued...

[Table of Contents](.)
