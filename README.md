# maddis-os

A tiny operating system built from scratch.
No Linux code inside. No C library. Every line was written for this OS.

It was inspired by Jonathan Blow's idea that operating systems got too complicated,
and that a simpler OS, built for today's many-core computers, would be better.

It also follows a rule from Terry Davis (TempleOS): **one person should be able to read and
understand the whole OS.** maddis-os is about 660 lines (C + a little assembly) and about 22 KB.

---

## Is it Linux?

**No.** Here is how the layers stack up when you run it:

```
Windows                      (your real PC)
  └── WSL / Ubuntu           (Linux, running inside Windows)
        └── QEMU             (a program that PRETENDS to be a whole computer)
              └── maddis-os  (our OS, running on that pretend computer)
```

- Linux only runs **QEMU**, the pretend computer.
- maddis-os runs **on** the pretend computer. It never calls Linux.
- It's like a game console emulator on a laptop: the game isn't made of the laptop.

How we know there's no Linux inside:

- It's built with `-nostdlib -ffreestanding`. That means: **use no outside code at all.**
- Even simple things like `print` and "compare two words" are written by hand in `kernel.c`.
- It talks straight to the (pretend) hardware: the screen chip, the clock, the power button.

---

## Is it faster than Linux?

**Not yet, and that's not what it tests.**

- QEMU *pretends* to be a RISC-V chip. Pretending is much slower than real hardware.
- The `speedtest` command compares **two ways of talking to the OS, inside maddis-os**:
  - **Old way (trap):** the app stops, jumps into the OS, then comes back. Linux, Windows and Mac work like this.
  - **New way (mailbox):** the app drops numbers in shared memory and keeps going. The OS on another core picks them up.
- The mailbox usually wins by about 2x to 6x. The exact number changes each run.

---

## The big idea in one picture

```
   CORE 0: the OS                      CORE 1: the app
   ---------------                     ----------------
   never sleeps                        does its work
   checks the keyboard  <-- MAILBOX <--  drops messages here
   checks the mailbox      (shared       never traps into the OS
                            memory)
```

- Most OSes make the app **stop and wait** every time it needs the OS.
- maddis-os gives the OS **its own core**. The app just leaves notes in a mailbox.
- Nobody stops. Nobody waits.

---

## What happens when it boots (step by step)

1. QEMU turns on the pretend computer with **2 cores**.
2. Both cores start at the same spot: `_start` in `start.S`.
3. Each core asks "which core am I?" and gets its own stack (scratch memory).
4. **Core 0** jumps to `kmain()`. That's the OS.
5. **Core 1** jumps to `app_main()`. That's the app.
6. The OS clears the screen, prints the neon blue logo, and shows the `maddis-os>` prompt.
7. The OS loops forever. Each time around it checks:
   - Is there a message in the mailbox? Print it.
   - Did you press a key? Handle it.
8. The app loops forever, waiting for a job from the OS.

---

## The files

| File | What it does |
|---|---|
| `start.S` | The very first code that runs. Sends core 0 to the OS and core 1 to the app. Also holds the "old way" trap handler. |
| `kernel.c` | The whole OS: screen, keyboard, disk driver, mailboxes, file system, commands, speed test. |
| `kernel.ld` | Tells the builder where in memory the OS goes (address `0x80000000`). |
| `Makefile` | The build recipe. `make` builds, `make run` starts it. |

---

## The file system

Simple on purpose. A file is **a name + its bytes**. That's it.

| Today (Linux / Windows) | maddis-os |
|---|---|
| About 8 layers between an app and the disk | About 2 |
| 3+ traps to read a file (`open`, `read`, `close`) | No traps |
| Each file has an owner, a group, permissions, 3 timestamps, links... | Name + bytes |
| Supports dozens of file system types | One |

**How it's saved:** the whole file system is one block of raw bytes in memory.
Every time something changes, maddis-os copies that block straight to the disk (`disk.img`).
When it boots, it copies the block back. No converting, no layers.

Limits: 16 files, 1024 bytes each, 7 folders (one level deep).

### Files through the mailbox

The app on core 1 never touches files itself. It drops a request in a **file mailbox**
("save app.txt", "read app.txt"), and the OS on core 0 does the job and puts the answer back.

```
App (core 1):  "save app.txt = ..."  --> FILE MAILBOX -->  OS (core 0) saves it
App (core 1):  "read app.txt"        --> FILE MAILBOX -->  OS (core 0) sends it back
```

Type `appfiles` to watch it happen. 2 file jobs, 0 traps.

---

## Commands

| Type this | What it does |
|---|---|
| `help` | show all commands |
| `clear` | clean the screen |
| `echo hi` | print what you type |
| `bytes hi` | see letters as bytes (8 on/off switches) |
| `ls` | list files, folders and how many bytes they use |
| `write a.txt hi` | make a file called `a.txt` with `hi` inside |
| `write a.txt` | then paste or type many lines, end with a line that is just `.` |
| `cat a.txt` | show what's in a file |
| `rm a.txt` | delete a file |
| `mkdir notes` | make a folder |
| `cd notes` | go into a folder (`cd ..` or `cd /` to go back) |
| `pwd` | show which folder you're in |
| `size` | how many bytes maddis-os and your files use |
| `appfiles` | the app on core 1 saves and reads a file through the mailbox |
| `badapp` | a bad app tries to read your files directly. The chip's walls stop it |
| `send hi` | send a message to the other maddis-os window |
| `sendfile a.txt` | send a file to the other maddis-os window |
| `net` | network status |
| `whoami` | who you are |
| `uname` | what OS this is |
| `cores` | show what each core is doing |
| `run 20` | core 1 sends 20 messages through the mailbox |
| `speedtest` | old trap way vs. new mailbox way |
| `exit` or **Ctrl+C twice** | turn off maddis-os (files stay on the disk) |

---

## The walls (safety)

The app on core 1 runs in **user mode**, locked in a room by the chip itself (RISC-V **PMP**):

| Memory | The app can... |
|---|---|
| OS code | read and run it, but not change it |
| Its own room (mailboxes, its stack) | read and write |
| Everything else (your files, the disk, the screen) | **nothing. The chip blocks it** |

If the app breaks a rule, the chip stops it, the OS prints a `[WALL]` message, and the app is restarted.
Your files stay safe. Type `badapp` to see it. The walls cost well under 1 KB, because the chip does the checking.

---

## Two maddis-os talking to each other

Two windows, connected by a pretend network cable. No internet layers (no IP, no TCP):
just raw packets in our own tiny format that starts with `MDOS`.

1. In one Ubuntu window: `make a`  (start this one first)
2. In a second Ubuntu window: `make b`
3. In either one: `send hello`, or `sendfile poem.txt`

Window B keeps its own disk, called `disk-b.img`.

---

## How to run it (Ubuntu / WSL)

Install the tools once:

```
sudo apt install -y gcc-riscv64-unknown-elf qemu-system-misc
```

Build and start (the first time, this also makes an empty 1 MB disk called `disk.img`):

```
make run
```

Erase all your files: `make wipe`

Leave with **Ctrl+C twice**.

---

## What it can't do (yet)

- Small disk: 16 files of 1 KB each.
- No internet. The network only talks to another maddis-os.
- No graphics, windows or mouse. Just text.
- Only runs on a pretend RISC-V computer (QEMU), not on your real PC's chip.

## Ideas for next steps

- **Bigger disk:** more and bigger files.
- **AMD version:** boot from a USB stick on a real PC.
- **More apps on more cores**, each with its own mailbox.
