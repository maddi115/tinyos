# The race: maddis-os vs real Linux

Same pretend computer (QEMU, RISC-V, 2 cores). Same job: the app sends 200000 numbers to the OS.

- `linux-Image`      a tiny real Linux 6.6 kernel (built with `linux-6.6.config`)
- `race-initrd.gz`   the test program, packed so Linux runs it right after booting
- `race.c`           the source code of the test program

Run it from the tinyos folder:

    make race

Compare with maddis-os:

    make run        then type: speedtest
