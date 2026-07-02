============================================
TinyRC
============================================

TinyRC is a Tiny Research Computer built from
an LC-3-compatible virtual machine and a tiny
operating system.

The project started as a simple LC-3 VM. Now
It includes supervisor/user mode separation,
trap vectors, exception handling, memory protection,
and memory-mapped I/O devices.Its goal is to explore
how a small CPU, VM, and operating system fit together.

--------------------------------------------
Compile the VM with a C compiler:

cc -Wall -Wextra -std=c11 vm/tinyrc-vm.c -o tinyrc-vm

--------------------------------------------
Run

Run the virtual machine with the OS object file first,
followed by one or more user programs:

./tinyrc-vm supplies/lc3os.obj supplies/hello.obj

You can use PennSim.jar to compile the assembly code.

--------------------------------------------
Example Program

A normal user program can start at address x3000:

.ORIG x3000

LEA R0, MSG
PUTS
HALT

MSG .STRINGZ "Hello from TinyRC.\n"

.END

--------------------------------------------
License

TinyRC is licensed under the GNU General Public
License version 3.

See the GPLv3 license text for details.
