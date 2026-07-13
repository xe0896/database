CC = cc
CFLAGS = -Wall -Wextra -g
HEADERS = protocol.h typedef.h offsets.h

all: server client

server: server.c protocol.c $(HEADERS)
	$(CC) $(CFLAGS) -pthread server.c protocol.c -o server

client: client.c protocol.c $(HEADERS)
	$(CC) $(CFLAGS) client.c protocol.c -o client

clean:
	rm -f server client

.PHONY: all clean
