# TinyOS

A tiny operating system built from scratch, inspired by Jonathan Blow's idea
of a simpler OS. No Linux code inside.

- Core 0 runs the OS. It never sleeps.
- Core 1 runs the app. It talks to the OS through a mailbox in shared memory, with no traps.
- `speedtest` compares the old trap way vs the mailbox way.

## Run it (Ubuntu / WSL)

    sudo apt install -y gcc-riscv64-unknown-elf qemu-system-misc
    make run

Type `help` for commands. Press Ctrl+C twice to leave.
