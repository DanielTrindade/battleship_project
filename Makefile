CC = gcc
CFLAGS = -Wall -I battleship

all: battleserver battleclient

battleserver: server/battleserver.c battleship/battleship.h
	$(CC) $(CFLAGS) -o server/battleserver server/battleserver.c

battleclient: client/battleclient.c battleship/battleship.h
	$(CC) $(CFLAGS) -o client/battleclient client/battleclient.c

clean:
	rm -f server/battleserver client/battleclient
