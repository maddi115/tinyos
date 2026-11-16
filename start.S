# start.S - first code that runs. Core 0 = OS, core 1 = app.
.section .text.boot
.global _start
_start:
    csrr  t0, mhartid
    la    sp, stack_top
    li    t1, 4096
    mul   t2, t0, t1
    sub   sp, sp, t2
    la    t1, trap_entry       # where to go when the app traps or hits a wall
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
    call  app_core_setup       # build the walls, then start the app in user mode
    j     park

# ---- every trap from the app lands here ----
.align 4
.global trap_entry
trap_entry:
    csrr  t0, mcause
    li    t1, 8                # 8 = the app asked the OS on purpose ("ecall")
    bne   t0, t1, wall_hit
    # THE OLD WAY: the app stopped and came into the OS. Do the OS job, go back.
    la    t0, os_sum           # the OS job: add the app's number to a total
    ld    t1, 0(t0)
    add   t1, t1, a0
    sd    t1, 0(t0)
    csrr  t0, mepc             # go back to the app, one step after the ecall
    addi  t0, t0, 4
    csrw  mepc, t0
    mret

# ---- the app hit a wall: the chip stopped it ----
wall_hit:
    la    t0, wall_cause       # remember what it tried
    csrr  t1, mcause
    sd    t1, 0(t0)
    la    t0, wall_addr        # ...and where
    csrr  t1, mtval
    sd    t1, 0(t0)
    la    t0, app_job          # cancel its job
    sw    zero, 0(t0)
    la    t0, wall_hits        # tell the OS (core 0 is watching this number)
    ld    t1, 0(t0)
    addi  t1, t1, 1
    sd    t1, 0(t0)
    la    sp, app_stack_top    # restart the app fresh, still walled in
    la    t0, app_main
    csrw  mepc, t0
    mret
