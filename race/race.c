// race.c - the SAME job as maddis-os "speedtest", but running on real Linux.
// The app sends 200000 numbers to "the OS" two ways:
//   OLD WAY: one Linux system call (a trap) per number
//   NEW WAY: a mailbox in shared memory, read by a second thread on core 0
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <time.h>
#include <unistd.h>

#define TEST_COUNT 200000
#define BIG 1024

static unsigned long now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000UL + t.tv_nsec / 1000;
}
static void pin(int core) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(core, &s);
    sched_setaffinity(0, sizeof s, &s);
}

static struct { unsigned long head, tail; unsigned long slot[BIG]; } box;
static unsigned long os_sum;

static void *os_core(void *x) {              // plays "the OS", on core 0
    pin(0);
    unsigned long done = 0, sum = 0;
    while (done < TEST_COUNT) {
        unsigned long h = __atomic_load_n(&box.head, __ATOMIC_ACQUIRE);
        while (done < h) { sum += box.slot[done % BIG]; done++; }
        __atomic_store_n(&box.tail, done, __ATOMIC_RELEASE);
    }
    os_sum = sum;
    return 0;
}

static void bar(unsigned long v, unsigned long big) {
    unsigned long n = 40 * v / big; if (n < 1) n = 1;
    printf("    "); for (unsigned long i = 0; i < n; i++) putchar('#'); printf("\n");
}

int main(void) {
    mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
    int null = open("/dev/null", O_WRONLY);
    setvbuf(stdout, 0, _IONBF, 0);
    pin(1);                                   // the app lives on core 1, like maddis-os

    printf("\n  ===== REAL LINUX, same pretend computer =====\n");
    printf("  Same job two ways: the app sends 200000 numbers to the OS.\n");

    for (int round = 1; round <= 3; round++) {
        // OLD WAY: a Linux system call for every number
        unsigned long t0 = now_us();
        for (unsigned long i = 1; i <= TEST_COUNT; i++) write(null, &i, sizeof i);
        unsigned long old_t = now_us() - t0;

        // NEW WAY: mailbox in shared memory, "OS" thread on core 0
        box.head = box.tail = 0;
        pthread_t th; pthread_create(&th, 0, os_core, 0);
        t0 = now_us();
        unsigned long h = 0;
        for (unsigned long i = 1; i <= TEST_COUNT; i++) {
            while (h - __atomic_load_n(&box.tail, __ATOMIC_ACQUIRE) == BIG) {}
            box.slot[h % BIG] = i; h++;
            if ((h & 63) == 0) __atomic_store_n(&box.head, h, __ATOMIC_RELEASE);
        }
        __atomic_store_n(&box.head, h, __ATOMIC_RELEASE);
        pthread_join(th, 0);
        unsigned long new_t = now_us() - t0;
        int ok = os_sum == (unsigned long)TEST_COUNT * (TEST_COUNT + 1) / 2;

        unsigned long big = old_t > new_t ? old_t : new_t;
        printf("\n  round %d\n", round);
        printf("  OLD WAY (Linux system call every time)\n    time: %lu microseconds\n", old_t); bar(old_t, big);
        printf("  NEW WAY (mailbox, OS thread on core 0)\n    time: %lu microseconds\n", new_t); bar(new_t, big);
        printf("  %s  mailbox was %.1fx faster\n", ok ? "right answer." : "WRONG ANSWER!", (double)old_t / new_t);
    }
    printf("\n  done. turning off...\n");
    reboot(RB_POWER_OFF);
    return 0;
}
