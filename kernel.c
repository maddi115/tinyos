// kernel.c - maddis-os. Your own tiny OS. No Linux code inside.
//
//   Core 0 = the OS.  It owns the screen and keyboard. It never sleeps.
//   Core 1 = the app. It talks to the OS through the mailbox.

#define UART 0x10000000UL                 // screen + keyboard chip
#define UART_DATA   (*(volatile unsigned char *)(UART + 0))
#define UART_STATUS (*(volatile unsigned char *)(UART + 5))
#define POWER (*(volatile unsigned int *)0x100000UL)        // power button chip
#define CLOCK (*(volatile unsigned long *)0x200BFF8UL)      // ticks 10 million times a second

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
static int to_num(const char *s) { int n = 0; while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0'); return n; }

#define LOAD(x)     __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

// ---------- THE MAILBOX for text messages ----------
#define SLOTS 16
static struct { unsigned head, tail; char slot[SLOTS][48]; } mailbox;
static void mailbox_send(const char *text) {
    unsigned h = mailbox.head;
    while (h - LOAD(mailbox.tail) == SLOTS) {}
    add_text(mailbox.slot[h % SLOTS], text);
    STORE(mailbox.head, h + 1);
}
static int mailbox_take(char *out) {
    unsigned t = mailbox.tail;
    if (t == LOAD(mailbox.head)) return 0;
    add_text(out, mailbox.slot[t % SLOTS]);
    STORE(mailbox.tail, t + 1);
    return 1;
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

// ---------- jobs the OS can give the app ----------
#define JOB_NONE   0
#define JOB_TEXT   1
#define JOB_TRAPS  2
#define JOB_NUMBOX 3
static volatile int app_job = JOB_NONE, app_count = 0, app_alive = 0;

// ================= CORE 1: THE APP =================
void app_main(void) {
    STORE(app_alive, 1);
    for (;;) {
        int job = LOAD(app_job);
        if (job == JOB_NONE) continue;

        if (job == JOB_TEXT) {
            for (int i = 1; i <= app_count; i++) {
                char buf[48];
                add_num(add_text(buf, "hi from core 1, message "), i);
                mailbox_send(buf);
            }
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

    // OLD WAY: the app traps into the OS 200000 times
    os_sum = 0;
    unsigned long t0 = CLOCK;
    STORE(app_job, JOB_TRAPS);
    while (LOAD(app_job) != JOB_NONE) {}
    unsigned long old_time = CLOCK - t0;
    int old_ok = (LOAD(os_sum) == want);

    // NEW WAY: the app drops numbers in the mailbox, the OS on core 0 adds them
    unsigned long sum = 0, done = 0;
    numbox.head = numbox.tail = 0;
    t0 = CLOCK;
    STORE(app_job, JOB_NUMBOX);
    while (done < TEST_COUNT) {
        unsigned long h = LOAD(numbox.head);
        while (done < h) { sum += numbox.slot[done % BIG]; done++; }  // take a whole stack
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

// ---------- THE FILE SYSTEM (simple, Blow style) ----------
// Just a list of files in memory. No folders inside folders, no layers.
// A file = a name + its bytes. That's it.
// (Lives in memory, so files disappear when maddis-os turns off.)
#define MAX_FILES 16
#define NAME_LEN  16
#define DATA_LEN  128
static struct file { int used; int folder; char name[NAME_LEN]; char data[DATA_LEN]; int size; } files[MAX_FILES];

// ---------- FOLDERS (one level: / and folders inside it) ----------
#define MAX_FOLDERS 8
static struct { int used; char name[NAME_LEN]; } folders[MAX_FOLDERS] = { { 1, "" } };  // folder 0 = "/"
static int here = 0;                       // the folder you are in right now

static struct file *find_file(const char *name) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].used && files[i].folder == here && same(files[i].name, name)) return &files[i];
    return 0;
}
static int find_folder(const char *name) {
    for (int i = 1; i < MAX_FOLDERS; i++) if (folders[i].used && same(folders[i].name, name)) return i;
    return -1;
}
static void copy_max(char *d, const char *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int length(const char *s) { int n = 0; while (s[n]) n++; return n; }

static struct file *file_write(const char *name, const char *text) {
    struct file *f = find_file(name);
    for (int i = 0; !f && i < MAX_FILES; i++) if (!files[i].used) f = &files[i];
    if (!f) return 0;
    f->used = 1;
    f->folder = here;
    copy_max(f->name, name, NAME_LEN);
    copy_max(f->data, text, DATA_LEN);
    f->size = length(f->data);
    return f;
}

static void file_list(void) {
    int any = 0;
    if (here == 0) for (int i = 1; i < MAX_FOLDERS; i++) if (folders[i].used) {
        print("  " BLUE); print(folders[i].name); print("/" RESET "   (folder)\r\n");
        any = 1;
    }
    for (int i = 0; i < MAX_FILES; i++) if (files[i].used && files[i].folder == here) {
        print("  " BLUE); print(files[i].name); print(RESET "   ");
        print_num(files[i].size); print(" bytes\r\n");
        any = 1;
    }
    if (!any) print("  (no files)\r\n");
}

static void file_read(const char *name) {
    struct file *f = find_file(name);
    if (!f) { print("  no file called "); print(name); print("\r\n"); return; }
    print("  "); print(f->data); print("\r\n");
}

static void file_delete(const char *name) {
    struct file *f = find_file(name);
    if (!f) { print("  no file called "); print(name); print("\r\n"); return; }
    f->used = 0;
    print("  deleted "); print(name); print("\r\n");
}

// split "write notes.txt hello there" into name and text
static void write_command(const char *args) {
    char name[NAME_LEN]; int i = 0;
    while (*args && *args != ' ' && i < NAME_LEN - 1) name[i++] = *args++;
    name[i] = 0;
    if (*args == ' ') args++;
    if (!name[0] || !*args) { print("  type: write notes.txt hello there\r\n"); return; }
    struct file *f = file_write(name, args);
    if (!f) { print("  disk full (16 files max)\r\n"); return; }
    print("  saved "); print(f->name); print(" ("); print_num(f->size); print(" bytes)\r\n");
}

static void print_path(void) { print("/"); print(folders[here].name); }

static void make_folder(const char *name) {
    if (!*name) { print("  type: mkdir notes\r\n"); return; }
    if (here != 0) { print("  folders can only go in /  (type: cd /)\r\n"); return; }
    if (find_folder(name) >= 0) { print("  already exists\r\n"); return; }
    for (int i = 1; i < MAX_FOLDERS; i++) if (!folders[i].used) {
        folders[i].used = 1;
        copy_max(folders[i].name, name, NAME_LEN);
        print("  made folder "); print(folders[i].name); print("\r\n");
        return;
    }
    print("  too many folders (7 max)\r\n");
}

static void change_folder(const char *path) {
    if (path[0] == '~') path++;                      // treat ~ like /
    if (path[0] == '/') path++;
    if (!*path || same(path, "..")) { here = 0; return; }
    int f = find_folder(path);
    if (f < 0) { print("  no folder called "); print(path); print("   (make it with: mkdir "); print(path); print(")\r\n"); return; }
    here = f;
}

static void linux_hint(const char *line) {
    print("  '"); print(line); print("' is a Linux command, not a maddis-os one.\r\n");
    print("  type exit, then run it in your Ubuntu window (maddi/~$)\r\n");
}

static void command(char *line) {
    if (line[0] == '\'' || line[0] == '"') {           // so 'help' works too
        char *p = line; while (p[1]) { p[0] = p[1]; p++; } *p = 0;
        if (p > line && (p[-1] == '\'' || p[-1] == '"')) p[-1] = 0;
    }

    if (same(line, "help")) {
        print("  help          show this list\r\n");
        print("  clear         clean the screen\r\n");
        print("  echo hi       print what you type\r\n");
        print("  bytes hi      see what letters look like as bytes\r\n");
        print("  ls            list files\r\n");
        print("  write a.txt hi   make a file called a.txt with hi inside\r\n");
        print("  cat a.txt     show what is in a file\r\n");
        print("  rm a.txt      delete a file\r\n");
        print("  mkdir notes   make a folder\r\n");
        print("  cd notes      go into a folder (cd .. or cd / to go back)\r\n");
        print("  pwd           show which folder you are in\r\n");
        print("  whoami        who you are\r\n");
        print("  uname         what OS this is\r\n");
        print("  cores         show what each core is doing\r\n");
        print("  run 20        core 1 sends 20 messages through the mailbox\r\n");
        print("  speedtest     old trap way vs new mailbox way\r\n");
        print("  exit          turn off maddis-os and go back to Linux\r\n");
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
    else if (same(line, "pwd"))       { print("  "); print_path(); print("\r\n"); }
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
    else if (same(line, "exit"))      { print("  bye!\r\n"); POWER = 0x5555; for (;;) {} }
    else { print("  unknown command: "); print(line); print("   (type help)\r\n"); }
}

static void prompt(void) { print(BLUE "maddis-os:"); print_path(); print("> " RESET); }

static int ctrl_c = 0;   // how many times Ctrl+C was pressed in a row

void kmain(void) {
    file_write("readme.txt", "welcome to maddis-os! try: write a.txt hello");
    print("\033[2J\033[H" BLUE);
    print("\r\n");
    print("   __  __           _     _ _                     \r\n");
    print("  |  \\/  | __ _  __| | __| (_)___        ___  ___ \r\n");
    print("  | |\\/| |/ _` |/ _` |/ _` | / __|_____ / _ \\/ __|\r\n");
    print("  | |  | | (_| | (_| | (_| | \\__ \\_____| (_) \\__ \\\r\n");
    print("  |_|  |_|\\__,_|\\__,_|\\__,_|_|___/      \\___/|___/\r\n");
    print(RESET "\r\n");
    print("  core 0 = OS, core 1 = app. Built from scratch, no Linux inside.\r\n");
    print("  Type help to see commands. Press Ctrl+C twice to leave.\r\n\r\n");
    prompt();

    char line[64], msg[48];
    int n = 0;
    for (;;) {                                           // the OS never sleeps
        if (mailbox_take(msg)) {                         // 1) check the mailbox
            print("  [mailbox] "); print(msg); print("\r\n");
            if (same(msg, "done!")) prompt();
        }
        if (key_ready()) {                               // 2) check the keyboard
            char c = UART_DATA;
            if (c == 3) {                                // Ctrl+C
                if (ctrl_c) { print("\r\n  bye!\r\n"); POWER = 0x5555; for (;;) {} }
                ctrl_c = 1;
                print("^C   (press Ctrl+C again to exit)\r\n"); prompt();
                n = 0;
                continue;
            }
            ctrl_c = 0;
            if (c == '\r' || c == '\n') {
                print("\r\n");
                line[n] = 0;
                if (n > 0) command(line);
                n = 0;
                if (LOAD(app_job) != JOB_TEXT) prompt();
            }
            else if ((c == 127 || c == 8) && n > 0) { n--; print("\b \b"); }
            else if (c >= 32 && c < 127 && n < 63) { line[n++] = c; putc(c); }
        }
    }
}
