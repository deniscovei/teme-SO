// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* Prepare the connection buffer to send the reply header. */
	struct stat st;

	fstat(conn->fd, &st);
	conn->file_size = st.st_size;
	sprintf(conn->send_buffer, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", conn->file_size);
	conn->send_len = strlen(conn->send_buffer);
	conn->state = STATE_SENDING_DATA;
	dlog(LOG_INFO, "Sending header\n");
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* Prepare the connection buffer to send the 404 header. */
	struct stat st;

	fstat(conn->fd, &st);
	conn->file_size = st.st_size;
	sprintf(conn->send_buffer, "HTTP/1.1 404 Not Found\r\nContent-Length: %ld"
							"\r\nConnection: close\r\n\r\n", conn->file_size);
	conn->send_len = strlen(conn->send_buffer);
	conn->state = STATE_404_SENT;
	dlog(LOG_INFO, "Sending 404\n");
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (strstr(conn->request_path, AWS_REL_STATIC_FOLDER) == conn->request_path + 1)
		return RESOURCE_TYPE_STATIC;

	if (strstr(conn->request_path, AWS_REL_DYNAMIC_FOLDER) == conn->request_path + 1)
		return RESOURCE_TYPE_DYNAMIC;

	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* Initialize connection structure on given socket. */
	struct connection *conn = calloc(1, sizeof(struct connection));

	DIE(conn == NULL, "malloc");

	conn->sockfd = sockfd;
	conn->fd = -1;
	conn->state = STATE_INITIAL;

	return conn;
}

void connection_start_async_io(struct connection *conn)
{
	/* Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
	int rc;

	conn->piocb[0] = &conn->iocb;
	io_prep_pread(&conn->iocb, conn->fd, conn->recv_buffer, BUFSIZ, conn->file_pos);

	rc = io_submit(ctx, 1, conn->piocb);
	if (rc != 1) {
		perror("io_submit");
		exit(EXIT_FAILURE);
	}

	conn->piocb[0] = &conn->iocb;
	io_prep_pwrite(&conn->iocb, conn->sockfd, conn->recv_buffer, BUFSIZ, 0);

	rc = io_submit(ctx, 1, conn->piocb);
	if (rc != 1) {
		perror("io_submit");
		exit(EXIT_FAILURE);
	}
}

void connection_remove(struct connection *conn)
{
	/* Remove connection handler. */
	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;

	if (conn->fd != -1)
		close(conn->fd);

	free(conn);
}

int make_socket_non_blocking(int sockfd)
{
	int flags, s;

	flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1) {
		perror("fcntl");
		return -1;
	}
	flags |= O_NONBLOCK;
	s = fcntl(sockfd, F_SETFL, flags);
	if (s == -1) {
		perror("fcntl");
		return -1;
	}
	return 0;
}

void handle_new_connection(void)
{
	/* Handle a new connection request on the server socket. */
	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;

	/* Accept new connection. */
	sockfd = accept(listenfd, (SSA *) &addr, &addrlen);
	DIE(sockfd < 0, "accept");

	/* Set socket to be non-blocking. */
	rc = make_socket_non_blocking(sockfd);
	DIE(rc < 0, "make_socket_non_blocking");

	/* Instantiate new connection handler. */
	conn = connection_create(sockfd);
	DIE(conn == NULL, "connection_create");

	dlog(LOG_INFO, "New connection from %s:%d on socket %d\n",
			inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), sockfd);

	/* Add socket to epoll. */
	rc = w_epoll_add_ptr_in(epollfd, sockfd, conn);
	DIE(rc < 0, "w_epoll_add_ptr_in");

	/* Initialize HTTP_REQUEST parser. */
	http_parser *parser = &conn->request_parser;

	http_parser_init(parser, HTTP_REQUEST);
	parser->data = conn;
}

void receive_data(struct connection *conn)
{
	/* Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */
	ssize_t bytes_recv;

	do {
		bytes_recv = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ - conn->recv_len, 0);
		dlog(LOG_INFO, "Received %ld bytes\n", bytes_recv);

		if (bytes_recv < 0) {
			dlog(LOG_ERR, "Error receiving data\n");
			return;
		}

		if (bytes_recv == 0 || strstr(conn->recv_buffer, "\r\n\r\n")) {
			dlog(LOG_INFO, "Received request:\n%s\n", conn->recv_buffer);
			dlog(LOG_INFO, "Request received\n");
			return;
		}

		conn->recv_len += bytes_recv;
	} while (bytes_recv > 0);
}

int connection_open_file(struct connection *conn)
{
	/* Open file and update connection fields. */
	char filepath[BUFSIZ] = {0};

	strcat(filepath, AWS_DOCUMENT_ROOT);
	strcat(filepath, conn->request_path + 1);

	dlog(LOG_INFO, "Opening file %s\n", conn->filename);
	int fd = open(filepath, O_RDONLY);

	if (fd < 0) {
		perror("open");
		conn->state = STATE_SENDING_404;
		return -1;
	}

	dlog(LOG_INFO, "Opened file %s\n", filepath);
	conn->fd = fd;
	conn->state = STATE_SENDING_HEADER;

	return 0;
}

void connection_complete_async_io(struct connection *conn)
{
	/* Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */
	struct io_event event;
	int rc;

	rc = io_getevents(ctx, 1, 1, &event, NULL);
	if (rc != 1 || event.res < 0)
		return;

	conn->file_pos += event.res;

	rc = io_getevents(ctx, 1, 1, &event, NULL);
	if (rc != 1 || event.res != BUFSIZ)
		return;

	conn->file_size -= BUFSIZ;
	conn->state = ((conn->file_size == 0) ? STATE_DATA_SENT : STATE_ASYNC_ONGOING);
}

int parse_header(struct connection *conn)
{
	/* Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};

	return http_parser_execute(&conn->request_parser,
								&settings_on_path,
								conn->recv_buffer,
								conn->recv_len) != conn->recv_len ? -1 : 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* Send static data using sendfile(2). */
	ssize_t bytes_sent = 0, total_bytes_sent = 0;

	while (total_bytes_sent < conn->file_size) {
		bytes_sent = sendfile(conn->sockfd, conn->fd, NULL, conn->file_size);

		if (bytes_sent < 0)
			return STATE_DATA_SENT;

		total_bytes_sent += bytes_sent;
	}

	conn->file_size -= total_bytes_sent;

	return conn->file_size == 0 ? STATE_DATA_SENT : STATE_SENDING_DATA;
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	ssize_t bytes_sent = 0, total_bytes_sent = 0;

	while (total_bytes_sent < conn->send_len) {
		bytes_sent = send(conn->sockfd, conn->send_buffer + total_bytes_sent,
							conn->send_len - total_bytes_sent, 0);

		if (bytes_sent < 0) {
			perror("send");
			return -1;
		}

		total_bytes_sent += bytes_sent;
	}

	conn->send_len -= total_bytes_sent;

	if (conn->fd == -1)
		conn->state = STATE_DATA_SENT;
	else if (conn->res_type == RESOURCE_TYPE_STATIC)
		conn->state = connection_send_static(conn);
	else
		conn->state = STATE_ASYNC_ONGOING;

	return total_bytes_sent;
}


int connection_send_dynamic(struct connection *conn)
{
	/* Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	connection_start_async_io(conn);
	connection_complete_async_io(conn);

	return 0;
}


void handle_input(struct connection *conn)
{
	/* Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */
	int rc;

	switch (conn->state) {
	case STATE_INITIAL:
		dlog(LOG_INFO, "Initial state\n");
		conn->state = STATE_RECEIVING_DATA;
		break;
	case STATE_RECEIVING_DATA:
		dlog(LOG_INFO, "Receiving data\n");
		receive_data(conn);

		if (conn->recv_len <= 0) {
			perror("recv");
			connection_remove(conn);
			dlog(LOG_INFO, "Error receiving data\n");
			return;
		}

		conn->state = STATE_REQUEST_RECEIVED;
		break;
	case STATE_CONNECTION_CLOSED:
		dlog(LOG_INFO, "Connection closed\n");
		break;
	default:
		break;
	}

	rc = w_epoll_update_ptr_out(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_update_ptr_inout");
}

void handle_output(struct connection *conn)
{
	/* Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */
	int rc;

	dlog(LOG_INFO, "Connection status: %d\n", conn->state);

	switch (conn->state) {
	case STATE_REQUEST_RECEIVED:
		dlog(LOG_INFO, "Request received\n");
		rc = parse_header(conn);
		if (rc < 0) {
			dlog(LOG_ERR, "Error parsing header\n");
			conn->state = STATE_SENDING_404;
			connection_remove(conn);
			break;
		}
		conn->res_type = connection_get_resource_type(conn);
		connection_open_file(conn);
		dlog(LOG_INFO, "Request received: EXIT\n");
		break;
	case STATE_SENDING_404:
		dlog(LOG_INFO, "Sending 404: ENTER\n");
		connection_prepare_send_404(conn);
		dlog(LOG_INFO, "Sending 404: EXIT\n");
		break;
	case STATE_SENDING_HEADER:
		dlog(LOG_INFO, "Sending header: ENTER\n");
		connection_prepare_send_reply_header(conn);
		dlog(LOG_INFO, "Sending header: EXIT\n");
		break;
	case STATE_SENDING_DATA:
		dlog(LOG_INFO, "Sending data\n");
		rc = connection_send_data(conn);
		if (rc <= 0) {
			dlog(LOG_ERR, "Error sending data\n");
			connection_remove(conn);
			return;
		}
		break;
	case STATE_ASYNC_ONGOING:
		dlog(LOG_INFO, "Async ongoing\n");
		connection_send_dynamic(conn);
		break;
	case STATE_DATA_SENT:
		dlog(LOG_INFO, "Data sent\n");
		w_epoll_update_fd_in(epollfd, conn->sockfd);
		connection_remove(conn);
		break;
	case STATE_HEADER_SENT:
		dlog(LOG_INFO, "Header sent\n");
		w_epoll_update_fd_in(epollfd, conn->sockfd);
		connection_remove(conn);
		break;
	case STATE_404_SENT:
		dlog(LOG_INFO, "404 sent\n");
		connection_remove(conn);
		break;
	default:
		break;
	}

	if (conn->res_type == RESOURCE_TYPE_NONE) {
		rc = w_epoll_update_ptr_in(epollfd, conn->sockfd, conn);
		DIE(rc < 0, "w_epoll_update_ptr_inout");
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	if (event & EPOLLIN)
		handle_input(conn);

	if (event & EPOLLOUT)
		handle_output(conn);
}

int main(void)
{
	int rc;

	/* Initialize asynchronous operations. */
	rc = io_setup(128, &ctx);
	DIE(rc < 0, "io_setup");

	/* Initialize multiplexing. */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

	/* Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);
	DIE(listenfd < 0, "tcp_create_listener");

	/* Add server socket to epoll object*/
	rc = w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");

	/* Uncomment the following line for debugging. */
	dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* Wait for events. */
		rc = w_epoll_wait_infinite(epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");

		/* Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		if (rev.data.fd == listenfd) {
			if (rev.events & EPOLLIN)
				handle_new_connection();
			continue;
		}

		struct connection *conn = (struct connection *)rev.data.ptr;

		dlog(LOG_INFO, "Handle client\n");
		handle_client(rev.events, conn);
	}

	tcp_close_connection(listenfd);

	return 0;
}

