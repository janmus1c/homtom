CC=gcc
CFLAGS= 

all: mkttimage

.PHONY: install
install: mkttimage
	install -D mkttimage $(ROOT)/tools/mkttimage

mkttimage: mkttimage.o md5.o blowfish.o
	$(CC) -o mkttimage mkttimage.o md5.o blowfish.o

%.o: %.c
	$(CC) -c $<

.PHONY: clean
clean:
	rm -f mkttimage *.o
