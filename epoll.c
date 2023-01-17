#include "epoll.h"

#define EPOLL_TIMEOUT_INFINITE -1

int epoll_fd_operation ( int epoll_fd, int fd, int events, int op ) {
	struct epoll_event ev;
    
	ev.events = events;
	ev.data.fd = fd;

    if ( op == ADD ) 
        return epoll_ctl ( epoll_fd, EPOLL_CTL_ADD, fd, &ev );
    if ( op == MOD )
        return epoll_ctl ( epoll_fd, EPOLL_CTL_MOD, fd, &ev );
    if ( op == DEL)
        return epoll_ctl ( epoll_fd, EPOLL_CTL_DEL, fd, &ev );
    return -1;
}

int epoll_ptr_operation ( int epoll_fd, int fd, void *ptr, int events, int op ) {
	struct epoll_event ev;

	ev.events = events;
	ev.data.ptr = ptr;

    if ( op == ADD )
	    return epoll_ctl ( epoll_fd, EPOLL_CTL_ADD, fd, &ev );
    if ( op == MOD )
        return epoll_ctl ( epoll_fd, EPOLL_CTL_MOD, fd, &ev );
    if ( op == DEL )
        return epoll_ctl ( epoll_fd, EPOLL_CTL_DEL, fd, &ev );
    return -1;
}