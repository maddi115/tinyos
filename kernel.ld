/* kernel.ld - tells the builder where our OS goes in memory */
ENTRY(_start)
SECTIONS {
    . = 0x80000000;            /* the pretend computer starts running here */
    .text : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    . += 4096 * 4;             /* room for 4 cores' stacks */
    stack_top = .;
}
