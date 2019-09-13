CC := gcc
CFLAGS := -g -Wall
TARGET := pipe fifo socketpair uds tcp udp shm shm2

all: $(TARGET)

pipe: pipe.c
	$(CC) $(CFLAGS) -o $@ $<

fifo: fifo.c
	$(CC) $(CFLAGS) -o $@ $<

socketpair: socketpair.c
	$(CC) $(CFLAGS) -o $@ $<

uds: uds.c
	$(CC) $(CFLAGS) -o $@ $<

tcp: tcp.c
	$(CC) $(CFLAGS) -o $@ $<

udp: udp.c
	$(CC) $(CFLAGS) -o $@ $<

shm: shm.c
	$(CC) $(CFLAGS) -o $@ $<

shm2: shm2.c
	$(CC) $(CFLAGS) -lrt -o $@ $<

.PHONY: test
test:
	sudo ./run_tests.sh

clean:
	rm $(TARGET)
