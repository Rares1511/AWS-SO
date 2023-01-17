#include "epoll.h"
#include "debug.h"
#include "aws.h"
#include "util.h"
#include "http_parser.h"
#include <assert.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define TIMEOUT 10000
#define ERR_MSG "HTTP/1.0 404 Not Found\r\n\r\n"
#define OK_MSG "HTTP/1.0 200 OK\r\n\r\n"

int epoll_fd;
char* request_path;
http_parser request_parser;

int on_path ( http_parser *parser, const char *buf, size_t len ) {
	assert ( parser == &request_parser );
	memcpy ( request_path, buf, len );
	memcpy ( parser->data, buf, len );
	return 0;
}

http_parser_settings settings = {
	/* on_message_begin */ 0,
	/* on_header_field */ 0,
	/* on_header_value */ 0,
	/* on_path */ on_path,
	/* on_url */ 0,
	/* on_fragment */ 0,
	/* on_query_string */ 0,
	/* on_body */ 0,
	/* on_headers_complete */ 0,
	/* on_message_complete */ 0
};

int get_epoll_fd ( int listen_fd ) {
	int temp_fd = epoll_create ( 10 );
    epoll_fd_operation ( temp_fd, listen_fd, EPOLLIN, ADD ); 
	return temp_fd;
}

int get_listen_fd ( ) {
    struct sockaddr_in *address = malloc ( SOCKADDR_IN_SIZE );
	int listen_fd;
	int sock_opt;
	int rc;

	listen_fd = socket ( PF_INET, SOCK_STREAM, 0 );
	DIE ( listen_fd < 0, "socket" );

	sock_opt = 1;
	rc = setsockopt ( listen_fd, SOL_SOCKET, SO_REUSEADDR,
				&sock_opt, sizeof ( int ) );
	DIE ( rc < 0, "setsockopt" );

	memset ( address, 0, SOCKADDR_IN_SIZE );
	address->sin_family = AF_INET;
	address->sin_port = htons ( AWS_LISTEN_PORT );
	address->sin_addr.s_addr = INADDR_ANY;

	rc = bind ( listen_fd, ( struct sockaddr* ) address, SOCKADDR_IN_SIZE );
	DIE ( rc < 0, "bind" );

	rc = listen ( listen_fd, BACKLOG );
	DIE ( rc < 0, "listen" );

	return listen_fd;
}

void connection_remove ( struct connection *conn ) {
	close ( conn->sock_fd );
	conn->state = CONNECTION_CLOSED;
	free ( conn->path );
	free ( conn->recv_buffer );
	free ( conn->send_buffer );
	free ( conn );
}

struct connection *connection_create ( int sock_fd ) {
	struct connection *conn = malloc ( sizeof ( *conn ) );
	DIE ( conn == NULL, "malloc" );

	conn->path = malloc ( BUFSIZ );
	DIE ( conn->path == NULL, "malloc path" );
	conn->recv_buffer = malloc ( BUFSIZ );
	DIE ( conn->recv_buffer == NULL, "malloc path" );
	conn->send_buffer = malloc ( BUFSIZ );
	DIE ( conn->send_buffer == NULL, "malloc path" );

	conn->sock_fd = sock_fd;
	memset ( conn->recv_buffer, 0, BUFSIZ );
	memset ( conn->send_buffer, 0, BUFSIZ );

	return conn;
}

void new_connection ( int listen_fd ) {
    static int sock_fd;
	socklen_t addrlen = sizeof ( struct sockaddr_in );
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;

	sock_fd = accept ( listen_fd, ( struct sockaddr* ) &addr, &addrlen );
	DIE ( sock_fd < 0, "accept" );

	conn = connection_create ( sock_fd );

	rc = epoll_ptr_operation ( epoll_fd, sock_fd, conn, EPOLLIN, ADD );
	DIE ( rc < 0, "epoll_ctl" );
}

enum connection_state remove_connection ( struct connection *conn ) {
    int rc = epoll_ptr_operation ( epoll_fd, conn->sock_fd, conn, EPOLLIN, DEL );
	DIE ( rc < 0, "epoll_remove_ptr" );

	/* remove current connection */
	connection_remove ( conn );

	return CONNECTION_CLOSED;
}

int get_peer_address ( int sock_fd, char *buf, size_t len ) {
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof ( struct sockaddr_in );

	if ( getpeername ( sock_fd, ( struct sockaddr* ) &addr, &addrlen ) < 0)
		return -1;
	return 0;
}

enum connection_state receive_message ( struct connection *conn ) {
    ssize_t bytes_recv;
	int rc;
	char *abuffer = malloc ( 64 );

	rc = get_peer_address ( conn->sock_fd, abuffer, 64 );
	if ( rc < 0 ) {
		ERR ( "get_peer_address" );
		return remove_connection ( conn );
	}

	bytes_recv = recv ( conn->sock_fd, conn->recv_buffer + conn->recv_len, BUFSIZ, 0 );
	if ( bytes_recv < 0 ) return remove_connection ( conn );
	if ( bytes_recv == 0 ) return remove_connection ( conn );

	printf ( "--\n%s--\n", conn->recv_buffer );

	conn->recv_len += bytes_recv;
	conn->state = DATA_RECEIVED;

	return DATA_RECEIVED;
}

void open_file ( struct connection *conn ) {
	conn->fd = open ( conn->path, O_RDONLY );
	if ( conn->fd == -1 ) {
		sprintf ( conn->send_buffer, ERR_MSG );
		conn->send_len = strlen ( ERR_MSG );
	}
	else {
		struct stat stats;
		fstat ( conn->fd, &stats );
		conn->file_size = stats.st_size;
		sprintf ( conn->send_buffer, OK_MSG );
		conn->send_len = strlen ( OK_MSG );
	}
}

void client_request ( struct connection *conn ) {
	int rc;
	enum connection_state conn_state;

	conn_state = receive_message ( conn );
	if ( conn_state == CONNECTION_CLOSED ) return;

	http_parser_init ( &request_parser, HTTP_REQUEST );
	request_parser.data = malloc ( sizeof ( char* ) ); 
	int bytes_parsed = http_parser_execute ( &request_parser, &settings, 
	                   conn->recv_buffer, conn->recv_len );
	printf ( "%s\n", request_parser.data );
	if ( bytes_parsed != 0 )
		sprintf ( conn->path, "%s%s", AWS_DOCUMENT_ROOT, request_path + 1 );
	
	open_file ( conn );

	/* add socket to epoll for out events */
	rc = epoll_ptr_operation ( epoll_fd, conn->sock_fd, 
                                conn, EPOLLIN | EPOLLOUT, MOD );
	DIE ( rc < 0, "epoll_ptr" );
}

enum connection_state send_message ( struct connection *conn ) {
	ssize_t bytes_sent;
	int rc;
	char *abuffer = malloc ( 64 );

	rc = get_peer_address ( conn->sock_fd, abuffer, 64 );
	if ( rc < 0 ) {
		ERR ( "get_peer_address" );
		return remove_connection ( conn );
	}

	if ( conn->send_len > 0 ) {
		bytes_sent = send ( conn->sock_fd, conn->send_buffer, conn->send_len, 0 );
		if ( bytes_sent <= 0 ) return remove_connection ( conn );
		conn->send_len -= bytes_sent;
		return 0;
	}

	if ( conn->fd != -1 ) {
		if ( strstr ( conn->path, "static" ) ) {
			if ( conn->file_size > 0 ) {
				int bytes_sent = sendfile ( conn->sock_fd, conn->fd, NULL, conn->file_size );
				conn->file_size -= bytes_sent;
				return 0;
			}
			else 
				return remove_connection ( conn );
		}
		else if ( strstr ( conn->path, "dynamic" ) ) {
			// sall
		}
	}

	printf ( "--\n%s--\n", conn->send_buffer );

	rc = epoll_ptr_operation ( epoll_fd, conn->sock_fd, conn, EPOLLIN, MOD );
	DIE ( rc < 0, "epoll_update_ptr" );

	conn->state = DATA_SENT;

	return DATA_SENT;
}

int main ( void ) {
    int rc = 0;
    int listen_fd = get_listen_fd ( );
    epoll_fd = get_epoll_fd ( listen_fd );
	request_path = malloc ( BUFSIZ );
    while ( 5 > 4 ) {
        struct epoll_event *event = malloc ( EPOLL_EVENT_SIZE );

        rc = epoll_wait ( epoll_fd, event, 1, TIMEOUT );
        DIE ( rc < 0, "epoll_wait" );
        printf ( "received event\n" );

        if ( event->data.fd == listen_fd ) {
            if ( event->events & EPOLLIN ) 
                new_connection ( listen_fd );
        }
        else {
            if ( event->events & EPOLLIN )
                client_request ( event->data.ptr );
            if ( event->events & EPOLLOUT )
                send_message ( event->data.ptr );
        }
    }

    close ( listen_fd );
    close ( epoll_fd );

    return 0;

}