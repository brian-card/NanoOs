# 16-Feb-2026 - New Memory Manager

*\*We interrupt this program for a special announcement...\**

\*sigh\*  So, yes, I did return to working on my overlay userspace work for a bit, but I hit a snag.  I knew this day would come eventually, but the timing was inconvenient.

The memory allocator I initially wrote was a modified bump allocator.  The modification was that I kept track of metadata that allowed me to rewind the `next` pointer to the end of the previous last-allocated block any time the current last-allocated block was freed.  This provided automatic memory compaction as long as memory allocations and deallocations followed last-in, first-out (LIFO) ordering.  I have always ordered all memory allocations and deallocations in NanoOs in LIFO order, so this has always worked fine.

The risk, of course, is that if allocations and deallocations didn't follow strict LIFO ordering, gaps in allocations would start piling up and the next pointer would creep further and further up.  So, I put in enough metadata that anytime the last allocation was freed, it would skip over any gaps that had been created since the memory was allocated.  So, as long as the last block of memory was eventually freed, all the gaps would be accounted for and memory compaction would still work.  As long as the software was mostly well-behaved, this strategy worked fine.

*But...*

In a multiprocessing system, if you hit the allocations and deallocations just right, they can overlap each other in such a way that the last pointer effectively never gets freed.  And, loading a new overlay into the system caused that exact scenario to happen every single time.  The end result was that I could only run a handfull of commands from the file system (three, I think) before I ran out of memory and could do nothing else.  Soooooo... A new strategy had to be devised.

Now, the challenge here is that we're simulating a PDP-11/20.  I only have 32 KB to work with (which is actually 8 KB more than the PDP-11/20 had, but it's the closest I could get).  So, my metadata limits are pretty strict.  I'm also on a very slow processor (48 MHz if memory serves me correctly), so the solution I came up with had to be as fast as possible.  Unfortunately, I knew I was going to have to make some sacrifices to both relative to the solution I had previously, but there was nothing I could do about that.

Most advanced memory allocation algorithms were right out.  8 KB of that 32 KB is used for the overlays and stacks are just over 1 KB and there are 9 processes.  Then, there's a section of memory at the bottom that the Arduino&reg; libraries use for themselves.  After all is said and done, I'm looking at around 6 KB of dynamic memory on the Nano 33 IoT that I'm using.  Just about anything that a full-featured OS with megabytes of memory would use would be inappropriate for such an environment.

Just about all the memory allocations and deallocations in the OS are LIFO ordered due to that being the optimal case for the modified bump allocator (and, let's face it, that's just a sane way to do it), so I wanted to optimize for that case.  I still wanted that to be as close to O(1) as possible.  But, in order to avoid the memory overlap problem, I was going to have to give up blindly allocating from the end of memory.  New logic - and hence new metadata - would be required to avoid that.

When I wrote the original memory manager, I started allocating from the bottom of the memory manager's stack and walked backward from there.  That was because, initially, I didn't know where the bottom of RAM was.  These days, I have a better memory map in use, so one of the things I wanted to change was starting from the bottom and working my way up the way a heap really should grow.

OK, so goals are understood.  What's the closest I could get?  First off, the only way to get close to constant time in *ANY* case was to have a doubly-linked list.  This addressed several issues:

1. When removing from the allocated list, it was simply a matter of setting `prev->next` and `next->prev` to splice out a piece of allocated memory.
2. When freeing a block of memory that was between two other free blocks, it allowed for fast memory compaction.
3. The presence of the `prev` pointers allowed for traversing the free list in reverse order on a free.
 - This allowed the LIFO free case to effectively be O(1).
4. The presence of the `next` pointers allowed for traversing the free list in forward order on a malloc.
 - This avoids the issue of memory gaps becoming unusable.

Then, the madness started.  The metadata for each block, a `MemNode`, has to be carved out of the available memory each time there's an allocation.  Allocation from the end of memory is pretty simple.  Allocation from somewhere in the middle was a nightmare.  I struggled to keep all the sizes and pointers straight.  Doing the memory compaction involved the same kind of pointer and size arithmetic.  I eventually broke down and used the new Claude&reg; model to track down some of the issues.  It found two places I had forgotten to update pointers.  It still got the size issues wrong.  (Fortunately, I spotted and fixed that myself.)

So, now, I have a new, functional memory manager that meets all of the requirements.  I'm still not crazy about it, though.  I'm not very fond of having one free list and one allocated list and using the `prev` and `next` pointers for two different purposes.  It would be simpler to have just one list that was used for all memory.  The problem then, though, is that I'd have to also track whether or not the block of memory was allocated.  I wasn't willing to do that.  The `MemNode` now has two pointers, a size, and a process ID owner which takes more memory than I would like considering this is an embedded environment.  I wasn't willing to add a fifth piece of metadata to it.  I suppose I might be able to get clever and hide it in something like the owner PID since it's only a single bit, but I wasn't sure about doing that.  The problem with doing that is that - even though that saves memroy - it complicates the evaluation logic.  The compiled code becomes larger because it's having to parse out a piece of a field and I also have to be mindful about the binary size I'm generating.  I dunno.  I may come back and reevaluate this at some point in the future.

Anyway, when I finally got everything put back together, I was able to run any number of user commands that I wanted!!  SUCCESS!!  Mission accomplished.  This strategy is still susceptible to fragmentation in bad enough cases, but it's much better than what it was.  For an embedded system that really only allows for simple memory management, it's not bad.

The main reason I'm embarking on this quest to move code to overlays is to free up space in the kernel.

*\*We now return you to our program already in progress.\**

To be continued...

[Table of Contents](.)
