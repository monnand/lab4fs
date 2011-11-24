CC=gcc
CFLAGS=-g
mklab4fs: mklab4fs.o
	$(CC) $(CFLAGS) -o $@ $^
mklab4fs.o: mklab4fs.c
	$(CC) -c $(CFLAGS) -o $@ $<
