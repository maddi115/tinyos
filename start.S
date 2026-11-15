# start.S - first code that runs. Core 0 = OS, core 1 = app.
.section .text.boot
.global _start
_start:
    csrr  t0, mhartid
    la    sp, stack_top
    li    t1, 4096
    mul   t2, t0, t1
    sub   sp, sp, t2
    la    t1, trap_entry       # where to go when an app "traps" into the OS
    csrw  mtvec, t1
    beqz  t0, os_core
    li    t1, 1
    beq   t0, t1, app_core
park:
    wfi
    j     park
os_core:
    call  kmain
    j     park
app_core:
    call  app_main
    j     park

# ---- THE OLD WAY: a trap ----
# The app runs "ecall". The core STOPS the app, jumps here, does the OS job,
# then jumps back to the app. This is how Linux, Windows and Mac work.
.align 4
.global trap_entry
trap_entry:
    la    t0, os_sum           # the OS job: add the app's number to a total
    ld    t1, 0(t0)
    add   t1, t1, a0
    sd    t1, 0(t0)
    csrr  t0, mepc             # go back to the app, one step after the ecall
    addi  t0, t0, 4
    csrw  mepc, t0
    mret
