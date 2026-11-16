/* kernel.ld - tells the builder where our OS goes in memory */
ENTRY(_start)
SECTIONS {
    . = 0x80000000;            /* the pretend computer starts running here */
    .text : { *(.text.boot) *(.text*) }
    text_end = .;              /* end of the code (used by the "size" command) */
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    data_end = .;              /* end of code + data (what gets loaded) */
    .bss : { *(.bss*) *(COMMON) }
    kernel_end = .;            /* end of the whole OS in memory */
    . = ALIGN(16);
    . += 4096 * 4;             /* room for 4 cores' stacks */
    stack_top = .;
}
