/* Runtime probe for the ARMv4T toolchain: libc start-up, stdio, passwd lookup, getrandom and libgcc 64-bit division.
 * Prints one line per check and exits 0 when all pass. Built twice by scripts/build-armv4t-toolchain.sh: as is
 * (must run on an ARMv4T core) and with -DUSE_CLZ -march=armv5te (must die with SIGILL there). */
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

int main(int argc, char **argv)
{
	volatile unsigned long long a = 0x123456789abcdefULL, b = 0x1234567ULL;
	unsigned char buf[16];
	struct passwd *pw = getpwnam("root");
	int fails = 0;

	(void)argv;
	printf("stdio: ok\n");
	printf("getpwnam(root): %s\n", pw ? pw->pw_shell : "missing");
	if (getrandom(buf, sizeof buf, GRND_NONBLOCK) == (ssize_t)sizeof buf)
		printf("getrandom: ok\n");
	else
		printf("getrandom: unavailable\n"), fails++;
	if (a / b == 0x100000079ULL && a % b == 0x40ULL)
		printf("udivdi3: ok\n");
	else
		printf("udivdi3: wrong %llx %llx\n", a / b, a % b), fails++;
#ifdef USE_CLZ
	printf("clz: %d\n", __builtin_clz((unsigned)argc << 20));
#endif
	return fails ? 1 : 0;
}
