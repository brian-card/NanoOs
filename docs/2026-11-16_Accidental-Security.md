# 16-Nov-2026 - Accidental Security: There are things that even root can't do

[Back a few months ago](2026-07-05_Scalability.md), I added HAL and IPC capabilities to NanoOs. The last thing I wanted to add was directory capabilities. I should note before I go any further for anyone who takes a look at the code and does a deep-dive: These are not yet true capabilities in the "unforgeable handle" sense of the term. I'm also currently only working on systems that have no memory protection, so they're only as good as the programs are trusted. The current state of things is a beginning, not an end.

Anyway, my idea for directory capabilities was to implement a system that answered the question, "What operations can a process perform on which directory trees?" My thoughts were that I wanted to allow specific operations for top-level directories that would apply to that directory and all of the content below it. Just as an example, a user's home directory might have read and write capabilities, but not execute. Execute might only be applied for $HOME/bin and below.

So, I was going through the implementation of this and I kind of backed myself into a corner when thinking about what the capabilities of the root directories should be, and particularly with respect to the root user. By default, I didn't want users to be able to list the contents of "/" and I didn't want root to be able to do absolutely anything. Where I really ran into trouble, though, was /etc/shadow. I had no issue with root being able to edit /etc/passwd but I just fundamentally didn't like the idea that root could even read /etc/shadow.

I then had an epiphany about my OS: root should administer userspace, not the entire operating system. To that end, what users - including root - see as "/" should not actually be the root of the real filesystem. The real root of the filesystem should hold namespaces, not directories. What userspace sees as "/" is actually located at "/userspace" on disk. No user can directly access any other namespace.

One of the consequences of this design is that I had to backpedal on a shortcut I created for listing the contents of directories. I had created an `istat` function that performed a stat given an inode. With the idea that there are directories that can't been accessed, that became insecure because any process could provide any raw inode they wanted. I had to convert that logic to a `dirstat` function that took a pointer to a `struct dirent` which, in turn, contained a handle to the actual DIR that backed the dirent. I was pleased that when I got all that put together, there was no noticeable difference to providing the raw inode.

The last thing I needed to fully close up the namespace issue was to convert FILE pointers to handles. I did that work yesterday. That made it impossible for a user program to forge a structure that gave themselves access to something in a namespace other than "userspace", again within the limits of not having hardware-protected memory.

Something I did have to fundamentally change in my OS to support this was the privilege level at which `login` runs. Only kernel or executive processes have the ability to reach into non-userspace namespaces and doing password validation requires accessing the "secure" namespace where the shadow file now lives. So, `login` is now an executive-level process that drops into the supervisor privilege level and drops its enhanced capabilities before exec-ing the user shell. At that point, no user, including root, can access anything other than the "userspace" namespace via file operations.

The fact that getty and login start as a executive processes isn't a security issue, though. I moved the program under the "/system" namespace, which means it's a trusted program that no user can update. I also moved the old /etc/config.sys under "/system" as well so that nothing can manually update what starts at boot and bypass trusted configurations.

Overall, I'm pretty pleased with how this turned out. I didn't plan to gain this level of security from this exercise, so it was an unexpected benefit. HOORAY!!

To be continued...

[Table of Contents](.)
