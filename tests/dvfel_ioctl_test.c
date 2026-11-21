// SPDX-License-Identifier: MIT
/* Interface test for /dev/dvfel on an idle box, adapted from the checks in
 * signde's fel-ioctl.c. Built freestanding as 32-bit ARM, like the players.
 *
 *   dvfel_ioctl_test         run every interface check
 *   dvfel_ioctl_test hold N  own a session for N seconds, then close it
 */
#include "dvfel_s7d_uapi.h"

_Static_assert(sizeof(struct dvfel_sc2_info) == 64, "info");
_Static_assert(sizeof(struct dvfel_sc2_el) == 32, "el");
_Static_assert(sizeof(struct dvfel_sc2_hint) == 16, "hint");
_Static_assert(sizeof(struct dvfel_sc2_done) == 72, "done");

#define EPERM 1
#define EBADF 9
#define EBUSY 16
#define EFAULT 14
#define EINVAL 22
#define ENOTTY 25
#define O_RDONLY 0
#define O_RDWR 2
#define O_CLOEXEC 0x80000

static long sc(long n, long a, long b, long c, long d)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;
	register long r3 __asm__("r3") = d;

	__asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3) : "memory", "cc");
	return r0;
}

#define sys_fork() sc(2, 0, 0, 0, 0)
#define sys_write(f, b, n) sc(4, f, (long)(b), n, 0)
#define sys_open(p, fl) sc(5, (long)(p), fl, 0, 0)
#define sys_close(f) sc(6, f, 0, 0, 0)
#define sys_ioctl(f, c, a) sc(54, f, (long)(c), (long)(a), 0)
#define sys_wait(p, s) sc(114, p, (long)(s), 0, 0)
#define sys_exit(c) sc(248, c, 0, 0, 0)
#define sys_getuid() sc(199, 0, 0, 0, 0)

void *memset(void *d, int c, unsigned int n)
{
	volatile unsigned char *p = d;

	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

static void put(const char *s)
{
	unsigned int n = 0;

	while (s[n])
		n++;
	sys_write(1, s, n);
}

static void putn(long v)
{
	char b[16];
	int i = 15;
	int neg = v < 0;
	unsigned long u = neg ? -v : v;

	b[i] = 0;
	do {
		b[--i] = '0' + u % 10;
		u /= 10;
	} while (u);
	if (neg)
		b[--i] = '-';
	put(&b[i]);
}

static void msleep(long ms)
{
	long ts[2] = { ms / 1000, (ms % 1000) * 1000000 };

	sc(162, (long)ts, 0, 0, 0);
}

static int failures, passes;

static void expect(long result, int error, const char *name)
{
	int ok = error ? result == -error : result == 0;

	put(ok ? "PASS " : "FAIL ");
	put(name);
	if (!ok) {
		put(" result=");
		putn(result);
		put(" expected=");
		putn(-error);
		failures++;
	} else {
		passes++;
	}
	put("\n");
}

static long start_retry(int fd)
{
	long r = -EBUSY;

	for (int i = 0; i < 40 && r == -EBUSY; i++) {
		r = sys_ioctl(fd, DVFEL_IOC_SC2_START, 0);
		if (r == -EBUSY)
			msleep(100);
	}
	return r;
}

static int str_eq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int hold(long secs)
{
	int fd = sys_open("/dev/dvfel", O_RDWR | O_CLOEXEC);
	long r;

	if (fd < 0) {
		put("open failed\n");
		return 2;
	}
	r = start_retry(fd);
	put("hold START=");
	putn(r);
	put("\n");
	if (r)
		return 1;
	msleep(secs * 1000);
	sys_close(fd);
	put("hold closed\n");
	return 0;
}

static int run(void)
{
	static struct dvfel_sc2_info info;
	struct dvfel_sc2_done d = { 0 };
	struct dvfel_sc2_hint hint = { .reserved = 1 };
	struct dvfel_sc2_el el = { .fd = -1 };
	unsigned int flags;
	int a = sys_open("/dev/dvfel", O_RDWR | O_CLOEXEC);
	int b = sys_open("/dev/dvfel", O_RDWR | O_CLOEXEC);
	long pid;
	int status = 0;

	if (a < 0 || b < 0) {
		put("open /dev/dvfel failed\n");
		return 2;
	}
	put("uid=");
	putn(sys_getuid());
	put("\n");
	expect(sys_ioctl(a, DVFEL_IOC_SC2_INFO, &info), 0, "INFO");
	expect(str_eq(info.version, DVFEL_SC2_VERSION) ? 0 : -1, 0, "version matches");
	put("version=");
	put(info.version);
	put("\n");
	expect(sys_ioctl(a, _IO('F', 99), 0), ENOTTY, "unknown command");
	expect(sys_ioctl(a, _IOR('F', 20, int), &info), ENOTTY, "wrong structure size");
	expect(sys_ioctl(a, DVFEL_IOC_SC2_INFO, 1), EFAULT, "invalid user pointer");
	expect(sys_ioctl(b, DVFEL_IOC_SC2_WAIT_DONE, &d), EPERM, "WAIT without session");

	expect(sys_ioctl(a, DVFEL_IOC_SC2_START, 0), 0, "START");
	expect(sys_ioctl(b, DVFEL_IOC_SC2_START, 0), EBUSY, "second owner START");
	expect(sys_ioctl(b, DVFEL_IOC_SC2_WAIT_DONE, &d), EPERM, "nonowner completion read");
	expect(sys_ioctl(b, DVFEL_IOC_SC2_STOP, 0), EPERM, "nonowner STOP");
	flags = 3;
	expect(sys_ioctl(b, DVFEL_IOC_SC2_FLUSH, &flags), EPERM, "nonowner FLUSH");
	flags = 4;
	expect(sys_ioctl(a, DVFEL_IOC_SC2_FLUSH, &flags), EINVAL, "invalid flush bits");
	flags = 3;
	expect(sys_ioctl(a, DVFEL_IOC_SC2_FLUSH, &flags), 0, "valid FLUSH");
	expect(sys_ioctl(a, DVFEL_IOC_SC2_BL_HINT, &hint), EINVAL, "reserved hint field");
	hint.reserved = 0;
	expect(sys_ioctl(b, DVFEL_IOC_SC2_BL_HINT, &hint), EPERM, "nonowner hint");
	expect(sys_ioctl(a, DVFEL_IOC_SC2_BL_HINT, &hint), 0, "valid hint");
	expect(sys_ioctl(b, DVFEL_IOC_SC2_EL_QUEUE, &el), EPERM, "nonowner EL queue");
	expect(sys_ioctl(a, DVFEL_IOC_SC2_EL_QUEUE, &el), EBADF, "invalid dma-buf fd");
	el.reserved = 1;
	expect(sys_ioctl(a, DVFEL_IOC_SC2_EL_QUEUE, &el), EINVAL, "reserved EL field");
	el.reserved = 0;
	el.fd = sys_open("/dev/null", O_RDONLY);
	expect(sys_ioctl(a, DVFEL_IOC_SC2_EL_QUEUE, &el), EINVAL, "non-dma-buf fd");
	sys_close(el.fd);
	d.timeout_ms = 1001;
	expect(sys_ioctl(a, DVFEL_IOC_SC2_WAIT_DONE, &d), EINVAL, "unbounded completion wait");
	memset(&d, 0xa5, sizeof(d));
	d.timeout_ms = 0;
	expect(sys_ioctl(a, DVFEL_IOC_SC2_WAIT_DONE, &d), 0, "empty completion read");
	expect(d.count, 0, "empty completion count");

	pid = sys_fork();
	if (pid == 0) {
		struct dvfel_sc2_done w = { .timeout_ms = 1000 };

		sys_exit(sys_ioctl(a, DVFEL_IOC_SC2_WAIT_DONE, &w) == -EPERM ? 0 : 1);
	}
	msleep(100);
	expect(sys_ioctl(a, DVFEL_IOC_SC2_STOP, 0), 0, "STOP while WAIT sleeps");
	sys_wait(pid, &status);
	expect(status, 0, "sleeping waiter rejected after STOP");

	expect(start_retry(b), 0, "next owner after drain");
	sys_close(a);
	expect(sys_ioctl(b, DVFEL_IOC_SC2_BL_HINT, &hint), 0, "nonowner close keeps session");
	sys_close(b);
	a = sys_open("/dev/dvfel", O_RDWR | O_CLOEXEC);
	expect(start_retry(a), 0, "owner close releases session");
	expect(sys_ioctl(a, DVFEL_IOC_SC2_STOP, 0), 0, "final STOP");
	sys_close(a);

	put("passes=");
	putn(passes);
	put(" failures=");
	putn(failures);
	put("\n");
	return failures ? 1 : 0;
}

__attribute__((used)) void cmain(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	long secs = 0;

	if (argc == 3 && str_eq(argv[1], "hold")) {
		for (const char *p = argv[2]; *p; p++)
			secs = secs * 10 + (*p - '0');
		sys_exit(hold(secs));
	}
	sys_exit(run());
}

__asm__(".text\n.global _start\n.type _start,%function\n_start:\n"
	"mov r0, sp\nbic sp, sp, #7\nbl cmain\n");
