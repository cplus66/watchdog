all: watchdog
CFLAGS=-g

watchdog:	watchdog.o

clean:
	rm -f *.o watchdog
