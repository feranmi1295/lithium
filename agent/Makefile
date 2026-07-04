CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude -g
LIBS    = -lsodium -lpthread
SRC     = src/main.c src/identity.c src/heartbeat.c \
          src/fingerprint.c src/server.c src/job_runner.c \
          src/client.c
OUT     = lithium

all:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LIBS)

clean:
	rm -f $(OUT)

install:
	cp $(OUT) /usr/local/bin/lithium

.PHONY: all clean install
