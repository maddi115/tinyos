# make      -> build TinyOS
# make run  -> start it in the pretend computer (quit: Ctrl+A then X)
.RECIPEPREFIX = >
CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -ffreestanding -nostdlib -O2 -Wall -Wl,--no-warn-rwx-segments

kernel.elf: start.S kernel.c kernel.ld
> $(CC) $(CFLAGS) -T kernel.ld start.S kernel.c -o kernel.elf

run: kernel.elf
> qemu-system-riscv64 -machine virt -smp 2 -m 128M -bios none -nographic -kernel kernel.elf

clean:
> rm -f kernel.elf
race:
> qemu-system-riscv64 -machine virt -smp 2 -m 256M -nographic -kernel race/linux-Image -initrd race/race-initrd.gz -append "console=ttyS0 quiet"
.PHONY: run clean race
