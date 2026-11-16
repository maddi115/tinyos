# make        -> build maddis-os
# make run    -> start it (files are saved in disk.img)
# make race   -> real Linux runs the same speed test
# make a      -> window A, with a network cable to window B (start this first)
# make b      -> window B (in a second Ubuntu window), connects to window A
# make wipe   -> erase the disks (all your files)
.RECIPEPREFIX = >
CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -ffreestanding -nostdlib -O2 -msmall-data-limit=0 -Wall -Wl,--no-warn-rwx-segments
QEMU = qemu-system-riscv64 -machine virt -smp 2 -nographic
NET_A = -netdev stream,id=n0,server=on,addr.type=inet,addr.host=127.0.0.1,addr.port=4321 -device virtio-net-device,netdev=n0,bus=virtio-mmio-bus.1
NET_B = -netdev stream,id=n0,server=off,addr.type=inet,addr.host=127.0.0.1,addr.port=4321 -device virtio-net-device,netdev=n0,bus=virtio-mmio-bus.1
DISK_B = -global virtio-mmio.force-legacy=false -drive file=disk-b.img,if=none,format=raw,id=d0 -device virtio-blk-device,drive=d0,bus=virtio-mmio-bus.0
DISK = -global virtio-mmio.force-legacy=false -drive file=disk.img,if=none,format=raw,id=d0 -device virtio-blk-device,drive=d0,bus=virtio-mmio-bus.0

kernel.elf: start.S kernel.c kernel.ld
> $(CC) $(CFLAGS) -T kernel.ld start.S kernel.c -o kernel.elf

disk.img disk-b.img:
> dd if=/dev/zero of=$@ bs=1024 count=1024 2>/dev/null

run: kernel.elf disk.img
> $(QEMU) -m 128M -bios none $(DISK) -kernel kernel.elf

a: kernel.elf disk.img
> $(QEMU) -m 128M -bios none $(DISK) $(NET_A) -kernel kernel.elf

b: kernel.elf disk-b.img
> $(QEMU) -m 128M -bios none $(DISK_B) $(NET_B) -kernel kernel.elf

race:
> $(QEMU) -m 256M -kernel race/linux-Image -initrd race/race-initrd.gz -append "console=ttyS0 quiet"

clean:
> rm -f kernel.elf

wipe:
> rm -f disk.img disk-b.img

.PHONY: run a b clean race wipe
