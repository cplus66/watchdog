all: watchdog
CFLAGS=-g

watchdog:	watchdog.o

clean:
	rm *.o watchdog
