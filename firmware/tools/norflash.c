/* norflash DEV IMAGE START END [--write]: rewrite the erase blocks of an MTD char device in
 * [START, END) whose contents differ from IMAGE, verifying each block by read-back.
 * Without --write it only reports the blocks that differ. Output is unbuffered so a log
 * shows the last step reached if the kernel hangs; see docs/flash-safety.md. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct mtd_info_user {
	unsigned char type;
	unsigned int flags;
	unsigned int size;
	unsigned int erasesize;
	unsigned int oobblock;
	unsigned int oobsize;
	unsigned int ecctype;
	unsigned int eccsize;
};

struct erase_info_user {
	unsigned int start;
	unsigned int length;
};

#define MTD_CLEAR_BITS 1
#define MTD_ERASEABLE 4
#define MEMGETINFO _IOR('M', 1, struct mtd_info_user)
#define MEMERASE _IOW('M', 2, struct erase_info_user)

static int read_at(int fd, unsigned int offset, char *buf, unsigned int len)
{
	unsigned int done = 0;

	if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
		return -1;
	while (done < len) {
		int got = read(fd, buf + done, len - done);
		if (got <= 0)
			return -1;
		done += got;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct mtd_info_user info;
	struct erase_info_user erase;
	unsigned int start, end, block;
	char *want, *have;
	int dev, img, write_mode, changed = 0;

	if (argc < 5) {
		fprintf(stderr, "usage: norflash DEV IMAGE START END [--write]\n");
		return 2;
	}
	start = strtoul(argv[3], NULL, 0);
	end = strtoul(argv[4], NULL, 0);
	write_mode = argc > 5 && !strcmp(argv[5], "--write");
	dev = open(argv[1], write_mode ? O_RDWR : O_RDONLY);
	img = open(argv[2], O_RDONLY);
	if (dev < 0 || img < 0 || ioctl(dev, MEMGETINFO, &info)) {
		perror("norflash open/info");
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("mtd size 0x%x erasesize 0x%x flags 0x%x\n", info.size, info.erasesize, info.flags);
	if (write_mode && (info.flags & (MTD_CLEAR_BITS | MTD_ERASEABLE)) != (MTD_CLEAR_BITS | MTD_ERASEABLE)) {
		fprintf(stderr, "device does not report erasable, writable flash; refusing\n");
		return 1;
	}
	if (start % info.erasesize || end % info.erasesize || end > info.size || start >= end) {
		fprintf(stderr, "range must be erase-block aligned and inside the device\n");
		return 1;
	}
	want = malloc(info.erasesize);
	have = malloc(info.erasesize);
	for (block = start; block < end; block += info.erasesize) {
		if (read_at(img, block, want, info.erasesize) || read_at(dev, block, have, info.erasesize)) {
			perror("norflash read");
			return 1;
		}
		if (!memcmp(want, have, info.erasesize)) {
			printf("0x%06x same\n", block);
			continue;
		}
		changed++;
		if (!write_mode) {
			printf("0x%06x differs\n", block);
			continue;
		}
		printf("0x%06x erasing\n", block);
		erase.start = block;
		erase.length = info.erasesize;
		if (ioctl(dev, MEMERASE, &erase)) {
			perror("norflash erase");
			return 1;
		}
		printf("0x%06x erased, programming\n", block);
		if (lseek(dev, block, SEEK_SET) == (off_t)-1
		    || write(dev, want, info.erasesize) != (int)info.erasesize) {
			perror("norflash write");
			return 1;
		}
		if (read_at(dev, block, have, info.erasesize) || memcmp(want, have, info.erasesize)) {
			printf("0x%06x VERIFY FAILED\n", block);
			return 1;
		}
		printf("0x%06x written and verified\n", block);
	}
	printf("%d block(s) %s\n", changed, write_mode ? "rewritten" : "differ");
	return 0;
}
