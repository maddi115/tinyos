/* kernel.ld - tells the builder where our OS goes in memory */
ENTRY(_start)
SECTIONS {
    . = 0x80000000;            /* the pretend computer starts running here */
    .text : { *(.text.boot) *(.text*) }
    text_end = .;              /* end of the code (used by the "size" command) */
    .rodata : { *(.rodata*) *(.srodata*) }
    . = ALIGN(16);
    rodata_end = .;            /* the app may read + run everything up to here */
    .data : { *(.data*) *(.sdata*) }
    data_end = .;              /* end of code + data (what gets loaded) */

    /* THE APP'S ROOM: the only memory the app on core 1 may write */
    .app (NOLOAD) : ALIGN(4096) {
        app_start = .;
        *(.bss.app*)
        . = ALIGN(16);
        . += 4096;             /* the app's stack */
        app_stack_top = .;
        . = ALIGN(4096);
        app_end = .;
    }

    .bss : { *(.bss*) *(.sbss*) *(COMMON) }
    kernel_end = .;            /* end of the whole OS in memory */
    . = ALIGN(16);
    . += 4096 * 4;             /* room for 4 cores' stacks */
    stack_top = .;
}
