/* rawcopy SRC DST OFFSET LENGTH: copy LENGTH bytes from SRC at OFFSET to DST (dd is not in the stock BusyBox). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	static char buf[65536];
	long long offset, left;
	int in, out;

	if (argc != 5) {
		fprintf(stderr, "usage: rawcopy SRC DST OFFSET LENGTH\n");
		return 2;
	}
	offset = strtoll(argv[3], NULL, 0);
	left = strtoll(argv[4], NULL, 0);
	in = open(argv[1], O_RDONLY);
	out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (in < 0 || out < 0 || lseek(in, (off_t)offset, SEEK_SET) == (off_t)-1) {
		perror("rawcopy");
		return 1;
	}
	while (left > 0) {
		int want = left < (long long)sizeof buf ? (int)left : (int)sizeof buf;
		int got = read(in, buf, want);
		if (got <= 0)
			break;
		if (write(out, buf, got) != got) {
			perror("write");
			return 1;
		}
		left -= got;
	}
	fsync(out);
	close(out);
	printf("%lld bytes left\n", left);
	return left == 0 ? 0 : 1;
}
