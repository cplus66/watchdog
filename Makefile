all: watchdog
CFLAGS=-g

watchdog:	watchdog.o

clean:
	rm -f *.o watchdog

run:
	for ((i=0; i<10; i++)); do echo $$i; ./watchdog 0 0; done

stress:
	for ((i=0; i<100; i++)); do echo $$i; ./watchdog 0 0; done
