# make        -> build maddis-os
# make run    -> start it (files are saved in disk.img)
# make race   -> real Linux runs the same speed test
# make wipe   -> erase the disk (all your files)
.RECIPEPREFIX = >
CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -ffreestanding -nostdlib -O2 -Wall -Wl,--no-warn-rwx-segments
QEMU = qemu-system-riscv64 -machine virt -smp 2 -nographic
DISK = -global virtio-mmio.force-legacy=false -drive file=disk.img,if=none,format=raw,id=d0 -device virtio-blk-device,drive=d0,bus=virtio-mmio-bus.0

kernel.elf: start.S kernel.c kernel.ld
> $(CC) $(CFLAGS) -T kernel.ld start.S kernel.c -o kernel.elf

disk.img:
> dd if=/dev/zero of=disk.img bs=1024 count=1024 2>/dev/null

run: kernel.elf disk.img
> $(QEMU) -m 128M -bios none $(DISK) -kernel kernel.elf

race:
> $(QEMU) -m 256M -kernel race/linux-Image -initrd race/race-initrd.gz -append "console=ttyS0 quiet"

clean:
> rm -f kernel.elf

wipe:
> rm -f disk.img

.PHONY: run clean race wipe
