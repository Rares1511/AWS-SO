CC = gcc
CFLAGS = -g -Wall -Wextra

build: aws

aws: aws.o epoll.o http_parser.o
	$(CC) $(CFLAGS) $^ -o $@

http_parser.o: http_parser.c http_parser.h
	$(CC) $(CFLAGS) -c $< -o $@

aws.o: aws.c epoll.c epoll.h aws.h util.h debug.h http_parser.h
	$(CC) $(CFLAGS) -c $< -o $@

epoll.o: epoll.c epoll.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm *.o