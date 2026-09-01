# xx-Xxx-2026 - Agon Light 2 eZ80 Port

OK, I had originally intended to do a deep dive into what this effort took, but that's ridiculous.  This has taken nearly a month and there's simply no way to capture all the details of this effort in any readable document.  So, let me just do the **Reader's Digest** version.

To recap:  This effort was about porting NanoOs to the Agon Light 2.  This system has an ~18 MHz eZ80 CPU, 520 KB of RAM, and actual keyboard/video/mouse connections.  I chose this platform because it's the closest I can come to recreating an IBM XT I had growing up.

Porting to this platform marks several firsts for NanoOs:
- The first time the system has run on a microprocessor with external peripherals as opposed to an microcontroller with everything built-in
- The first time it's run in an environment with an acutal video console
- The first time it's had a HAL that didn't rely on pre-existing third-party hardware libraries

That last one was a biggie.  Not having anything to support me meant that I had to write a lot of stuff myself.  That I expected.  What I didn't expect was that I'd have to implement equivalents for hardware that was missing.

It turns out that there's no oscillator connected to the CPU on this board, so it doesn't have a real-time clock.  The video processor does and the way the system was intended to work was that the CPU would query the video processor for time.  The thing is, that protocol doesn't report fractions of a second, so that was no good as far as I was concerned.  What it did have were timer interrupts.  After some experiments, I settled on a timer that fired every 62.5 microseconds.  For a CPU that ran at about 18 MHz, a "clock" with sub-hundred-microsecond accuracy seemed like more than enough.

One of the more irritating aspects of this was when I hit what I think is a compiler bug.  Now, I know the compiler team would likely disagree with me and tell me that I was relying on undefined behavior, but I would actually hold my ground on this.  The issue was variables in use after a `setjmp` call.  The compiler had decided that the variables shortly after the call could reuse some of the stack slots occupied before the call.  That turned out to be invalid because one of the variable slots it reused was present within the `if` statement that guarded the `setjmp`.  To me, `setjmp` is part of the C standard and the compiler should have been aware of it and its implications.  Clearly, the compiler folks disagreed.

I should note, I had absolutely zero interest in writing this HAL.  To me, it was a necessary evil and not part of the core work I want to be doing with this project.  I want to be writing a good operating system, not becoming intimately familiar with the internals of every platform I port it to.

The platform usually runs an operating system called "MOS" and I started out by running NanoOs as a MOS application.  Development of the timer interrupts brought that practice to a screeching halt, however.  It turns out that the Interrupt Vector Table has to be located in the first 64 KB of address space, which is occupied by the 128 KB of flash.  The only way to install the timer IVT was to replace MOS on the flash.

This platform has an emulator, which Claude and I made heavy use of during development and debug.  I worked out most of the details of the platform-specific stuff on the emulator before attempting to run it on real hardware.  And, as is to be expected, that first attempt failed.  The problem was around the SPI/SD Card.  The emulator actually didn't implement all of the SD Card commands, so I had had to make some adjustments and fallbacks.  Real hardware had another "gotcha", though.  I had configured the SPI bus to run at 8 Mbps and that was something that the real hardware just wasn't capable of doing.  There was a footnote in the spec that basically said that it couldn't handle running at more than about 3 Mbps.  A clamp in the HAL fixed the issue.

Once I finally got past all that, I was able to load the filesystem off the disk and read program overlays into memory and run them.  HOORAY!!!  And, it is SLOOOOOOOOW!!!  It's just like working on an IBM XT from about 1985.  It's AWESOME!!!

After a few last tweaks to the HAL, I had functional parity with the other HALs already implemented.  This is far from over, though.  This is the beginning, not the end.  The whole point of moving to this platform was to be able to run on a real keyboard/video/mouse setup.  My 1996 IBM keyboard has been sitting here patiently for months waiting on a version of NanoOs to type on.  I'm so close but still so far away.  On this system, the video chip controls the VGA port, keyboard port, and the mouse connection.  Getting input from the keyboard and output to go to the screen means communicating with the video chip, which means writing another process driver.  That's something I'm looking forward to, actually, but something that's going to require some thought.  The CPU and video chips are connected via UART0 and the video chip can send data to the CPU asynchronously.  Up to this point, all my serial receives have been through polling but I'll now need to figure out a HAL mechanism for interrupt-driven I/O.  That's long overdue, honestly, and I'm glad I've finally graduated to a system where I can do that properly.

So, more to do, as always, but this is a great step in the right direction.

To be continued...

[Table of Contents](.)
