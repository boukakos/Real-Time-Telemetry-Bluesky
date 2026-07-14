CC = gcc
CFLAGS = -Wall -pthread -O2
LDFLAGS = -lwebsockets -lcjson

all: sysprog

sysprog: main.c
	$(CC) $(CFLAGS) main.c -o sysprog $(LDFLAGS)

clean:
	rm -f sysprog metrics_log.txt terminal_output.txt