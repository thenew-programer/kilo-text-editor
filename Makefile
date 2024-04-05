CC = gcc

kilo.bin: kilo.c
	$(CC) kilo.c -o kilo.bin -Wall -Wextra -pedantic -std=c99
