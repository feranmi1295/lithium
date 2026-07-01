CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude -g
LIBS    = -lsodium
SRC     = src/main.c src/identity.c src/heartbeat.c src/fingerprint.c
OUT     = lithium

all:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LIBS)

clean:
	rm -f $(OUT)

install:
	cp $(OUT) /usr/local/bin/lithium

.PHONY: all clean install
