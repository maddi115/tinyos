// kernel.c - maddis-os. Your own tiny OS. No Linux code inside.
//
//   Core 0 = the OS.  It owns the screen, keyboard and disk. It never sleeps.
//   Core 1 = the app. It talks to the OS only through mailboxes.
//
// Keep it simple (Terry Davis style): one person should be able to read all of it.

#define UART 0x10000000UL                 // screen + keyboard chip
#define UART_DATA   (*(volatile unsigned char *)(UART + 0))
#define UART_STATUS (*(volatile unsigned char *)(UART + 5))
#define POWER (*(volatile unsigned int *)0x100000UL)        // power button chip
#define CLOCK (*(volatile unsigned long *)0x200BFF8UL)      // ticks 10 million times a second

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long  u64;

// ---------- screen and keyboard ----------
static void putc(char c) { while ((UART_STATUS & 0x20) == 0) {} UART_DATA = c; }
static void print(const char *s) { while (*s) putc(*s++); }
static int key_ready(void) { return UART_STATUS & 0x01; }

#define BLUE  "\033[1;38;5;45m"   // neon blue
#define RESET "\033[0m"

// ---------- small text helpers (no C library here, so we make our own) ----------
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int starts(const char *s, const char *w) {
    while (*w) if (*s++ != *w++) return 0;
    return *s == 0 || *s == ' ';
}
static char *add_text(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *add_num(char *d, unsigned long n) {
    char t[24]; int i = 0;
    do { t[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i) *d++ = t[--i];
    *d = 0; return d;
}
static void print_num(unsigned long n) { char b[24]; add_num(b, n); print(b); }
static void print_commas(unsigned long n) {          // 11476 -> 11,476
    if (n >= 1000) { print_commas(n / 1000); putc(',');
        unsigned long r = n % 1000; putc('0' + r / 100); putc('0' + r / 10 % 10); putc('0' + r % 10); }
    else print_num(n);
}
static int to_num(const char *s) { int n = 0; while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0'); return n; }
static void copy_max(char *d, const char *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int length(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void copy_bytes(void *d, const void *s, unsigned long n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }

#define LOAD(x)     __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

// ---------- THE MAILBOX for text messages (app -> OS -> screen) ----------
#define SLOTS 16
static struct { unsigned head, tail; char slot[SLOTS][64]; } mailbox;
static void mailbox_send(const char *text) {
    unsigned h = mailbox.head;
    while (h - LOAD(mailbox.tail) == SLOTS) {}
    copy_max(mailbox.slot[h % SLOTS], text, 64);
    STORE(mailbox.head, h + 1);
}
static int mailbox_take(char *out) {
    unsigned t = mailbox.tail;
    if (t == LOAD(mailbox.head)) return 0;
    copy_max(out, mailbox.slot[t % SLOTS], 64);
    STORE(mailbox.tail, t + 1);
    return 1;
}

// ================= THE DISK =================
// A tiny driver for QEMU's "virtio" disk. Only core 0 uses it.
// The whole file system is saved as ONE block of raw bytes (no layers).
#define VIRTIO_BASE 0x10001000UL
static volatile u32 *vio = 0;                // the disk chip's control panel
#define VREG(off) vio[(off) / 4]

#define QNUM 8
struct vq_desc  { u64 addr; u32 len; u16 flags; u16 next; };
struct vq_avail { u16 flags; u16 idx; u16 ring[QNUM]; u16 unused; };
struct vq_used  { u16 flags; u16 idx; struct { u32 id; u32 len; } ring[QNUM]; u16 unused; };
static struct vq_desc  desc[QNUM]  __attribute__((aligned(4096)));
static struct vq_avail avail       __attribute__((aligned(4096)));
static struct vq_used  used        __attribute__((aligned(4096)));
static struct { u32 type; u32 reserved; u64 sector; } blk_req;
static volatile u8 blk_status;
static u16 used_seen = 0;

static int disk_init(void) {
    for (int i = 0; i < 8; i++) {                       // look in all 8 slots for a disk
        volatile u32 *r = (volatile u32 *)(VIRTIO_BASE + i * 0x1000UL);
        if (r[0] == 0x74726976 && r[1] == 2 && r[2] == 2) { vio = r; break; }
    }
    if (!vio) return 0;
    VREG(0x070) = 0;                                    // reset
    VREG(0x070) = 1 | 2;                                // "I see you" + "I have a driver"
    VREG(0x024) = 0; VREG(0x020) = 0;                   // no extra features
    VREG(0x024) = 1; VREG(0x020) = 1;                   // but speak modern virtio
    VREG(0x070) = 1 | 2 | 8;                            // features OK
    if (!(VREG(0x070) & 8)) return 0;
    VREG(0x030) = 0;                                    // queue 0
    if (VREG(0x034) < QNUM) return 0;
    VREG(0x038) = QNUM;
    VREG(0x080) = (u32)(u64)desc;   VREG(0x084) = (u32)((u64)desc >> 32);
    VREG(0x090) = (u32)(u64)&avail; VREG(0x094) = (u32)((u64)&avail >> 32);
    VREG(0x0a0) = (u32)(u64)&used;  VREG(0x0a4) = (u32)((u64)&used >> 32);
    VREG(0x044) = 1;                                    // queue ready
    VREG(0x070) = 1 | 2 | 8 | 4;                        // driver OK, disk is live
    return 1;
}

// read or write `bytes` (a multiple of 512) starting at sector 0
static int disk_rw(void *buf, u32 bytes, int write) {
    if (!vio) return 0;
    blk_req.type = write ? 1 : 0; blk_req.reserved = 0; blk_req.sector = 0;
    blk_status = 0xff;
    desc[0] = (struct vq_desc){ (u64)&blk_req, sizeof blk_req, 1, 1 };
    desc[1] = (struct vq_desc){ (u64)buf, bytes, (u16)(1 | (write ? 0 : 2)), 2 };
    desc[2] = (struct vq_desc){ (u64)&blk_status, 1, 2, 0 };
    avail.ring[avail.idx % QNUM] = 0;
    __sync_synchronize();
    avail.idx++;
    __sync_synchronize();
    VREG(0x050) = 0;                                    // ring the disk's doorbell
    while (*(volatile u16 *)&used.idx == used_seen) {}  // wait until the disk is done
    used_seen++;
    VREG(0x064) = VREG(0x060);
    return blk_status == 0;
}

// ---------- SPEED TEST ----------
#define TEST_COUNT 200000
#define BIG 1024
static struct { unsigned long head, tail; unsigned long slot[BIG]; } numbox;  // a mailbox for numbers
unsigned long os_sum;                    // the OS job: keep a total of the app's numbers

// Old way: one trap into the OS for every single number.
static void trap_into_os(unsigned long n) {
    register unsigned long a0 asm("a0") = n;
    asm volatile("ecall" : "+r"(a0) : : "t0", "t1", "memory");
}

// ================= THE FILE SYSTEM =================
// A file = a name + its bytes. Folders go one level deep.
// Saved to disk as one raw block every time something changes.
#define MAX_FILES   16
#define NAME_LEN    16
#define DATA_LEN    1024
#define MAX_FOLDERS 8
struct file   { int used; int folder; char name[NAME_LEN]; char data[DATA_LEN]; int size; };
struct folder { int used; char name[NAME_LEN]; };

static struct {
    u32 magic;                                   // "MDOS" = this disk has maddis-os files on it
    u32 version;
    struct folder folders[MAX_FOLDERS];
    struct file   files[MAX_FILES];
} fs __attribute__((aligned(512)));
#define FS_MAGIC 0x534f444d
#define FS_BYTES ((sizeof fs + 511) / 512 * 512)
static u8 disk_buf[FS_BYTES] __attribute__((aligned(512)));
static int here = 0;                             // the folder you are in right now (0 = "/")

static void fs_save(void) {
    if (!vio) return;
    copy_bytes(disk_buf, &fs, sizeof fs);
    disk_rw(disk_buf, FS_BYTES, 1);
}
static int fs_load(void) {
    if (!disk_rw(disk_buf, FS_BYTES, 0)) return 0;
    if (((u32 *)disk_buf)[0] != FS_MAGIC) return 0;
    copy_bytes(&fs, disk_buf, sizeof fs);
    return 1;
}

static struct file *find_file_in(int folder, const char *name) {
    for (int i = 0; i < MAX_FILES; i++)
        if (fs.files[i].used && fs.files[i].folder == folder && same(fs.files[i].name, name)) return &fs.files[i];
    return 0;
}
static int find_folder(const char *name) {
    for (int i = 1; i < MAX_FOLDERS; i++) if (fs.folders[i].used && same(fs.folders[i].name, name)) return i;
    return -1;
}
static struct file *file_write_in(int folder, const char *name, const char *text) {
    struct file *f = find_file_in(folder, name);
    for (int i = 0; !f && i < MAX_FILES; i++) if (!fs.files[i].used) f = &fs.files[i];
    if (!f) return 0;
    f->used = 1;
    f->folder = folder;
    copy_max(f->name, name, NAME_LEN);
    copy_max(f->data, text, DATA_LEN);
    f->size = length(f->data);
    fs_save();
    return f;
}
static int file_delete_in(int folder, const char *name) {
    struct file *f = find_file_in(folder, name);
    if (!f) return 0;
    f->used = 0;
    fs_save();
    return 1;
}

// ================= FILES THROUGH THE MAILBOX =================
// The app on core 1 never touches the file system itself.
// It drops a request in this mailbox, and the OS on core 0 answers.
#define OP_SAVE 1
#define OP_READ 2
#define OP_DEL  3
static struct {
    unsigned req_head, req_tail;          // app writes requests, OS reads them
    unsigned rep_head, rep_tail;          // OS writes replies, app reads them
    struct { int op; char name[NAME_LEN]; char data[DATA_LEN]; } req[4];
    struct { int ok; char data[DATA_LEN]; } rep[4];
} filebox;

// App side: ask the OS for a file job and wait for the answer. No traps.
static int app_file(int op, const char *name, const char *data, char *answer) {
    unsigned h = filebox.req_head;
    while (h - LOAD(filebox.req_tail) == 4) {}
    filebox.req[h % 4].op = op;
    copy_max(filebox.req[h % 4].name, name, NAME_LEN);
    copy_max(filebox.req[h % 4].data, data ? data : "", DATA_LEN);
    STORE(filebox.req_head, h + 1);                       // "request is ready"

    unsigned t = filebox.rep_tail;
    while (t == LOAD(filebox.rep_head)) {}                // wait for the OS's answer
    int ok = filebox.rep[t % 4].ok;
    if (answer) copy_max(answer, filebox.rep[t % 4].data, DATA_LEN);
    STORE(filebox.rep_tail, t + 1);
    return ok;
}

// OS side: if the app asked for something, do it and answer.
static void os_handle_filebox(void) {
    unsigned t = filebox.req_tail;
    if (t == LOAD(filebox.req_head)) return;
    int op = filebox.req[t % 4].op, ok = 0;
    const char *name = filebox.req[t % 4].name;
    unsigned h = filebox.rep_head;
    filebox.rep[h % 4].data[0] = 0;
    if (op == OP_SAVE) ok = file_write_in(0, name, filebox.req[t % 4].data) != 0;
    if (op == OP_DEL)  ok = file_delete_in(0, name);
    if (op == OP_READ) {
        struct file *f = find_file_in(0, name);
        if (f) { copy_max(filebox.rep[h % 4].data, f->data, DATA_LEN); ok = 1; }
    }
    filebox.rep[h % 4].ok = ok;
    STORE(filebox.req_tail, t + 1);
    STORE(filebox.rep_head, h + 1);                       // "answer is ready"
}

// ---------- jobs the OS can give the app ----------
#define JOB_NONE   0
#define JOB_TEXT   1
#define JOB_TRAPS  2
#define JOB_NUMBOX 3
#define JOB_FILES  4
static volatile int app_job = JOB_NONE, app_count = 0, app_alive = 0;

// ================= CORE 1: THE APP =================
void app_main(void) {
    STORE(app_alive, 1);
    for (;;) {
        int job = LOAD(app_job);
        if (job == JOB_NONE) continue;

        if (job == JOB_TEXT) {
            for (int i = 1; i <= app_count; i++) {
                char buf[64];
                add_num(add_text(buf, "hi from core 1, message "), i);
                mailbox_send(buf);
            }
            mailbox_send("done!");
        }
        else if (job == JOB_FILES) {              // the app uses files, only through mailboxes
            static char got[DATA_LEN];
            char line[64];
            mailbox_send("app (core 1): asking the OS to save app.txt...");
            int ok = app_file(OP_SAVE, "app.txt", "written by the app on core 1, through the mailbox", 0);
            mailbox_send(ok ? "OS (core 0): saved it" : "OS (core 0): could not save");
            mailbox_send("app (core 1): asking the OS to read app.txt back...");
            ok = app_file(OP_READ, "app.txt", 0, got);
            add_text(add_text(line, "OS (core 0) sent back: "), "");
            mailbox_send(ok ? line : "OS (core 0): no such file");
            if (ok) { copy_max(line, got, 60); mailbox_send(line); }
            mailbox_send("app: 2 file jobs, 0 traps. type ls to see app.txt");
            mailbox_send("done!");
        }
        else if (job == JOB_TRAPS) {              // OLD WAY
            for (unsigned long i = 1; i <= TEST_COUNT; i++)
                trap_into_os(i);                   // stop, go into the OS, come back
        }
        else if (job == JOB_NUMBOX) {             // NEW WAY
            unsigned long h = numbox.head;
            for (unsigned long i = 1; i <= TEST_COUNT; i++) {
                while (h - LOAD(numbox.tail) == BIG) {}   // full? wait a moment
                numbox.slot[h % BIG] = i;                  // drop it in the mailbox
                h++;
                if ((h & 63) == 0) STORE(numbox.head, h);  // tell the OS every 64
            }
            STORE(numbox.head, h);
        }
        STORE(app_job, JOB_NONE);
    }
}

// ================= CORE 0: THE OS =================
static void bar(unsigned long v, unsigned long biggest) {
    unsigned long n = 40 * v / biggest;
    if (n < 1) n = 1;
    print("    ");
    for (unsigned long i = 0; i < n; i++) putc('#');
    print("\r\n");
}

static void speedtest(void) {
    const unsigned long want = (unsigned long)TEST_COUNT * (TEST_COUNT + 1) / 2;
    print("  Same job two ways: the app sends 200000 numbers, the OS adds them up.\r\n");
    print("  running...\r\n");

    os_sum = 0;
    unsigned long t0 = CLOCK;
    STORE(app_job, JOB_TRAPS);
    while (LOAD(app_job) != JOB_NONE) {}
    unsigned long old_time = CLOCK - t0;
    int old_ok = (LOAD(os_sum) == want);

    unsigned long sum = 0, done = 0;
    numbox.head = numbox.tail = 0;
    t0 = CLOCK;
    STORE(app_job, JOB_NUMBOX);
    while (done < TEST_COUNT) {
        unsigned long h = LOAD(numbox.head);
        while (done < h) { sum += numbox.slot[done % BIG]; done++; }
        STORE(numbox.tail, done);
    }
    unsigned long new_time = CLOCK - t0;
    while (LOAD(app_job) != JOB_NONE) {}
    int new_ok = (sum == want);

    unsigned long biggest = old_time > new_time ? old_time : new_time;
    print("\r\n  OLD WAY (app traps into the OS every time)\r\n");
    print("    time: "); print_num(old_time / 10); print(" microseconds\r\n");
    print("    trips into the OS: 200000\r\n");
    bar(old_time, biggest);
    print("\r\n  NEW WAY (app uses the mailbox, OS on core 0)\r\n");
    print("    time: "); print_num(new_time / 10); print(" microseconds\r\n");
    print("    trips into the OS: 0\r\n");
    bar(new_time, biggest);
    print("\r\n");
    if (!old_ok || !new_ok) print("  warning: a total came out wrong!\r\n");
    else print("  both ways got the right answer.\r\n");
    if (new_time < old_time) {
        unsigned long x10 = old_time * 10 / new_time;
        print("  the new way was "); print_num(x10 / 10); putc('.'); print_num(x10 % 10);
        print("x faster.\r\n");
    } else {
        print("  the old way won this time.\r\n");
    }
}

static void bytes(const char *word) {
    if (!*word) { print("  type: bytes hi\r\n"); return; }
    print("  letter   number   the 8 switches (1 = on, 0 = off)\r\n");
    for (; *word; word++) {
        unsigned char c = *word;
        print("    "); putc(c == ' ' ? '_' : c); print("       ");
        if (c < 100) putc(' ');
        print_num(c); print("      ");
        for (int b = 7; b >= 0; b--) putc((c >> b) & 1 ? '1' : '0');
        print("\r\n");
    }
}

// ---------- file commands for you (they work in the folder you're in) ----------
static void print_path(void) { print("/"); print(fs.folders[here].name); }

static void file_list(void) {
    int count = 0; unsigned long total = 0;
    if (here == 0) for (int i = 1; i < MAX_FOLDERS; i++) if (fs.folders[i].used) {
        print("  " BLUE); print(fs.folders[i].name); print("/" RESET "   (folder)\r\n");
        count++;
    }
    for (int i = 0; i < MAX_FILES; i++) if (fs.files[i].used && fs.files[i].folder == here) {
        print("  " BLUE); print(fs.files[i].name); print(RESET "   ");
        print_num(fs.files[i].size); print(" bytes\r\n");
        total += fs.files[i].size; count++;
    }
    if (!count) { print("  (empty)\r\n"); return; }
    print("  ---\r\n  "); print_num(total); print(" bytes in this folder\r\n");
}

static void file_read(const char *name) {
    struct file *f = find_file_in(here, name);
    if (!f) { print("  no file called "); print(name); print("\r\n"); return; }
    print("  ");
    for (const char *p = f->data; *p; p++) {
        if (*p == '\n') print("\r\n  "); else putc(*p);
    }
    print("\r\n");
}

static void file_delete(const char *name) {
    if (file_delete_in(here, name)) { print("  deleted "); print(name); print("\r\n"); }
    else { print("  no file called "); print(name); print("\r\n"); }
}

static void make_folder(const char *name) {
    if (!*name) { print("  type: mkdir notes\r\n"); return; }
    if (here != 0) { print("  folders can only go in /  (type: cd /)\r\n"); return; }
    if (find_folder(name) >= 0) { print("  already exists\r\n"); return; }
    for (int i = 1; i < MAX_FOLDERS; i++) if (!fs.folders[i].used) {
        fs.folders[i].used = 1;
        copy_max(fs.folders[i].name, name, NAME_LEN);
        fs_save();
        print("  made folder "); print(fs.folders[i].name); print("\r\n");
        return;
    }
    print("  too many folders (7 max)\r\n");
}

static void change_folder(const char *path) {
    if (path[0] == '~') path++;
    if (path[0] == '/') path++;
    if (!*path || same(path, "..")) {
        if (here == 0) print("  you're already at / (the top). there is nothing above it.\r\n");
        here = 0; return;
    }
    int f = find_folder(path);
    if (f < 0) { print("  no folder called "); print(path); print("   (make it with: mkdir "); print(path); print(")\r\n"); return; }
    here = f;
}

static void linux_hint(const char *line) {
    print("  '"); print(line); print("' is a Linux command, not a maddis-os one.\r\n");
    print("  type exit, then run it in your Ubuntu window (maddi/~$)\r\n");
}

extern char _start[], text_end[], data_end[], kernel_end[];
static void size_report(void) {
    unsigned long code = text_end - _start, data = data_end - text_end, space = kernel_end - data_end;
    unsigned long used = 0; int n = 0;
    for (int i = 0; i < MAX_FILES; i++) if (fs.files[i].used) { used += fs.files[i].size; n++; }
    print("  code (the OS itself):   "); print_commas(code);  print(" bytes\r\n");
    print("  data (text, settings):  "); print_commas(data);  print(" bytes\r\n");
    print("  ---------------------------------------\r\n");
    print("  maddis-os total:        " BLUE); print_commas(code + data); print(" bytes" RESET "\r\n\r\n");
    print("  empty space in memory:  "); print_commas(space); print(" bytes (for files, mailboxes, disk buffer)\r\n");
    print("  your files:             "); print_num(n); print(" files, "); print_commas(used); print(" bytes\r\n");
    print("  disk:                   ");
    if (vio) { print_commas(FS_BYTES); print(" bytes saved per change\r\n"); }
    else print("none (files live in memory only)\r\n");
}

// ---------- paste mode: "write notes.txt" with no text ----------
static int  paste_mode = 0;
static char paste_name[NAME_LEN];
static char paste_buf[DATA_LEN];
static int  paste_len = 0;

static void write_command(const char *args) {
    char name[NAME_LEN]; int i = 0;
    while (*args && *args != ' ' && i < NAME_LEN - 1) name[i++] = *args++;
    name[i] = 0;
    if (*args == ' ') args++;
    if (!name[0]) { print("  type: write notes.txt hello there\r\n  or:   write notes.txt   (then paste many lines)\r\n"); return; }
    if (!*args) {
        copy_max(paste_name, name, NAME_LEN);
        paste_len = 0; paste_buf[0] = 0; paste_mode = 1;
        print("  paste or type lines. finish with a line that is just a dot:  .\r\n");
        return;
    }
    struct file *f = file_write_in(here, name, args);
    if (!f) { print("  disk full (16 files max)\r\n"); return; }
    print("  saved "); print(f->name); print(" ("); print_num(f->size); print(" bytes)\r\n");
}

static void paste_line(const char *line) {
    if (same(line, ".")) {
        paste_mode = 0;
        struct file *f = file_write_in(here, paste_name, paste_buf);
        if (!f) { print("  disk full (16 files max)\r\n"); return; }
        print("  saved "); print(f->name); print(" ("); print_num(f->size); print(" bytes)\r\n");
        return;
    }
    if (paste_len > 0 && paste_len < DATA_LEN - 1) paste_buf[paste_len++] = '\n';
    for (const char *p = line; *p && paste_len < DATA_LEN - 1; p++) paste_buf[paste_len++] = *p;
    paste_buf[paste_len] = 0;
    if (paste_len >= DATA_LEN - 1) print("  (file is full: 1024 bytes max)\r\n");
}

static void power_off(void);
static void tree(void) {
    print("  " BLUE "/" RESET "   (the top: nothing above this)\r\n");
    for (int i = 0; i < MAX_FILES; i++) if (fs.files[i].used && fs.files[i].folder == 0) {
        print("  |-- "); print(fs.files[i].name); print("   "); print_commas(fs.files[i].size); print(" bytes\r\n");
    }
    for (int d = 1; d < MAX_FOLDERS; d++) if (fs.folders[d].used) {
        print("  |-- " BLUE); print(fs.folders[d].name); print("/" RESET "\r\n");
        for (int i = 0; i < MAX_FILES; i++) if (fs.files[i].used && fs.files[i].folder == d) {
            print("  |     |-- "); print(fs.files[i].name); print("   "); print_commas(fs.files[i].size); print(" bytes\r\n");
        }
    }
}

static void command(char *line) {
    while (*line == ' ') line++;                         // ignore spaces at the start
    { int e = length(line); while (e > 0 && line[e - 1] == ' ') line[--e] = 0; }   // and at the end
    if (!*line) return;
    if (line[0] == '\'' || line[0] == '"') {           // so 'help' works too
        char *p = line; while (p[1]) { p[0] = p[1]; p++; } *p = 0;
        if (p > line && (p[-1] == '\'' || p[-1] == '"')) p[-1] = 0;
    }

    if (same(line, "help")) {
        print("  help          show this list\r\n");
        print("  clear         clean the screen\r\n");
        print("  echo hi       print what you type\r\n");
        print("  bytes hi      see what letters look like as bytes\r\n");
        print("  ls            list files and how many bytes they use\r\n");
        print("  write a.txt hi   make a file called a.txt with hi inside\r\n");
        print("  write a.txt   then paste many lines, end with a line: .\r\n");
        print("  cat a.txt     show what is in a file\r\n");
        print("  rm a.txt      delete a file\r\n");
        print("  mkdir notes   make a folder\r\n");
        print("  cd notes      go into a folder (cd .. or cd / to go back)\r\n");
        print("  pwd           show which folder you are in\r\n");
        print("  tree          show every folder and file\r\n");
        print("  size          how many bytes maddis-os and your files use\r\n");
        print("  appfiles      the app on core 1 saves a file through the mailbox\r\n");
        print("  whoami        who you are\r\n");
        print("  uname         what OS this is\r\n");
        print("  cores         show what each core is doing\r\n");
        print("  run 20        core 1 sends 20 messages through the mailbox\r\n");
        print("  speedtest     old trap way vs new mailbox way\r\n");
        print("  exit          turn off maddis-os (files stay on the disk)\r\n");
    }
    else if (same(line, "clear"))     print("\033[2J\033[H");
    else if (starts(line, "echo"))    { print("  "); print(line[4] ? line + 5 : ""); print("\r\n"); }
    else if (starts(line, "bytes"))   bytes(line[5] ? line + 6 : "");
    else if (same(line, "ls"))        file_list();
    else if (starts(line, "write"))   write_command(line[5] ? line + 6 : "");
    else if (starts(line, "cat"))     file_read(line[3] ? line + 4 : "");
    else if (starts(line, "rm"))      file_delete(line[2] ? line + 3 : "");
    else if (starts(line, "mkdir"))   make_folder(line[5] ? line + 6 : "");
    else if (starts(line, "cd"))      change_folder(line[2] ? line + 3 : "");
    else if (same(line, "tree"))      tree();
    else if (same(line, "pwd"))       { print("  "); print_path(); print("\r\n"); }
    else if (same(line, "size"))      size_report();
    else if (same(line, "appfiles"))  { print("  OS: asking core 1 to use files through the mailbox...\r\n"); STORE(app_job, JOB_FILES); }
    else if (starts(line, "git") || starts(line, "make") || starts(line, "sudo") || starts(line, "apt"))
                                      linux_hint(line);
    else if (same(line, "whoami"))    print("  user\r\n");
    else if (same(line, "uname"))     print("  maddis-os (not Linux, built from scratch)\r\n");
    else if (same(line, "hello"))     print("  hello from your own OS!\r\n");
    else if (same(line, "speedtest")) speedtest();
    else if (same(line, "cores")) {
        print("  core 0: OS   - running, you're talking to it\r\n");
        print(LOAD(app_alive) ? "  core 1: app  - running, waiting for a job\r\n"
                              : "  core 1: app  - not started\r\n");
    }
    else if (starts(line, "run")) {
        int n = line[3] ? to_num(line + 4) : 5;
        if (n <= 0) n = 5;
        if (n > 1000) n = 1000;
        print("  OS: asking core 1 to send messages...\r\n");
        app_count = n;
        STORE(app_job, JOB_TEXT);
    }
    else if (same(line, "exit"))      power_off();
    else { print("  unknown command: "); print(line); print("   (type help)\r\n"); }
}

static void prompt(void) {
    if (paste_mode) { print("  | "); return; }
    print(BLUE "maddis-os "); print_path(); print("> " RESET);
}

static int ctrl_c = 0;   // how many times Ctrl+C was pressed in a row

static void power_off(void) {
    print(vio ? "  bye! (your files are saved)\r\n" : "  bye! (no disk, so files are gone)\r\n");
    POWER = 0x5555;
    for (;;) {}
}

void kmain(void) {
    int disk_ok = disk_init();
    int loaded = disk_ok && fs_load();
    if (!loaded) {                                       // brand new disk (or no disk)
        fs.magic = FS_MAGIC; fs.version = 1;
        fs.folders[0].used = 1;
        file_write_in(0, "readme.txt", "welcome to maddis-os! try: write a.txt hello");
    }

    print("\033[2J\033[H" BLUE);
    print("\r\n");
    print("   __  __           _     _ _                     \r\n");
    print("  |  \\/  | __ _  __| | __| (_)___        ___  ___ \r\n");
    print("  | |\\/| |/ _` |/ _` |/ _` | / __|_____ / _ \\/ __|\r\n");
    print("  | |  | | (_| | (_| | (_| | \\__ \\_____| (_) \\__ \\\r\n");
    print("  |_|  |_|\\__,_|\\__,_|\\__,_|_|___/      \\___/|___/\r\n");
    print(RESET "\r\n");
    print("  core 0 = OS, core 1 = app. Built from scratch, no Linux inside.\r\n");
    print("  size: " BLUE); print_commas((unsigned long)(data_end - _start)); print(" bytes" RESET " (");
    print_commas((unsigned long)(text_end - _start)); print(" code + ");
    print_commas((unsigned long)(data_end - text_end)); print(" data). type size for more.\r\n");
    if (loaded)       print("  disk: found your saved files.\r\n");
    else if (disk_ok) print("  disk: new disk, ready. files will be saved.\r\n");
    else              print("  disk: none. files live in memory only.\r\n");
    print("  Type help to see commands. Press Ctrl+C twice to leave.\r\n\r\n");
    prompt();

    char line[128], msg[64];
    int n = 0;
    char last = 0;
    for (;;) {                                           // the OS never sleeps
        os_handle_filebox();                             // 1) did the app ask for a file?
        if (mailbox_take(msg)) {                         // 2) any text from the app?
            print("  [mailbox] "); print(msg); print("\r\n");
            if (same(msg, "done!")) prompt();
        }
        if (key_ready()) {                               // 3) did you press a key?
            char c = UART_DATA;
            if (c == 3) {                                // Ctrl+C
                if (ctrl_c) { print("\r\n"); power_off(); }
                ctrl_c = 1;
                if (paste_mode) { paste_mode = 0; print("^C   (paste cancelled)\r\n"); }
                else print("^C   (press Ctrl+C again to exit)\r\n");
                n = 0; last = 0;
                prompt();
                continue;
            }
            ctrl_c = 0;
            if (c == '\n' && last == '\r') { last = c; continue; }   // pasted Windows line ending: skip
            last = c;
            if (c == '\t') c = ' ';                                  // tabs become spaces
            if (c == '\r' || c == '\n') {
                print("\r\n");
                line[n] = 0;
                if (paste_mode) paste_line(line);
                else if (n > 0) command(line);
                n = 0;
                if (paste_mode || LOAD(app_job) == JOB_NONE) prompt();
            }
            else if ((c == 127 || c == 8) && n > 0) { n--; print("\b \b"); }
            else if (c >= 32 && c < 127 && n < 127) { line[n++] = c; putc(c); }
        }
    }
}
