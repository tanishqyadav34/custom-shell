CC = cc
CFLAGS = -Wall -Wextra -std=c99 -pedantic

all: shell

shell: shell.c
	$(CC) $(CFLAGS) -o shell shell.c

clean:
	rm -f shell
