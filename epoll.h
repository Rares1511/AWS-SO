#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 
#define BACKLOG 5
#define SOCKADDR_IN_SIZE sizeof ( struct sockaddr_in )
#define EPOLL_EVENT_SIZE sizeof ( struct epoll_event )

enum epoll_operation {
	ADD, 
	MOD,
	DEL
};

enum connection_state {
	DATA_RECEIVED,
	DATA_SENT,
	CONNECTION_CLOSED
};

struct connection {
	int sock_fd, fd;
	char *recv_buffer;
	size_t recv_len;
	char *send_buffer;
	size_t send_len;
	char *path;
	enum connection_state state;
	int file_size;
};

int epoll_fd_operation ( int epoll_fd, int fd, int events, int op );
int epoll_ptr_operation ( int epoll_fd, int fd, void *ptr, int events, int op );