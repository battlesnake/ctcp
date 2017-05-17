#if 0
(
set -eu
declare -r tmp="$(mktemp)"
trap "rm -f -- '$tmp'" EXIT ERR
gcc -O2 -std=gnu11 -Wall -Wextra -Werror -Ic_modules -DSIMPLE_LOGGING -DTEST_ctcp -o "$tmp" $(find -name '*.c') -lpthread
netcat -lp 9382 127.0.0.1 -e cat &
sleep 0.3
valgrind --quiet --leak-check=full --track-origins=yes "$tmp" 127.0.0.1 9382 "hello loopback" &
sleep 1
kill %1
wait
)
exit 0
#endif
#include <cstd/std.h>
#include <cstd/unix.h>
#include <cstruct/linked_list.h>
#include <resolve/resolve.h>
#include "select.h"
#include "socket.h"

/* Default error handlers */

typedef void error_handler(void *, const char *, const char *);

void socket_default_on_error(void *self, const char *func, const char *msg)
{
	(void) self;
	fprintf(stderr, "Socket error: %s failed with %s\n", func, msg);
}

static void socket_error_format(void *self, const char *func, const char *format, ...)
{
	va_list args;
	char error_buf[1000];
	va_start(args, format);
	vsnprintf(error_buf, sizeof(error_buf), format, args);
	va_end(args);
	error_handler *eh = *(error_handler **) self;
	/* Should never be NULL, is initialised in constructor */
	eh(self, func, error_buf);
}

socket_client_on_error *socket_client_default_error_handler =
	(socket_client_on_error *) socket_default_on_error;
socket_server_on_error *socket_server_default_error_handler =
	(socket_server_on_error *) socket_default_on_error;

#define socket_error(format, ...) socket_error_format(state, __func__, format "\n", ##__VA_ARGS__)

#define socket_error_num(what, addr, port) socket_error("failed to %s <" PRIfs ":" PRIfs "> (code %d)", what, prifs(addr), prifs(port), errno)

#define socket_error_msg(what, addr, port, msg) socket_error("failed to %s <" PRIfs ":" PRIfs "> (%s)", what, prifs(addr), prifs(port), msg)

/* Close all sockets on exit */

static struct list *fds;

static bool finaliser_bound;

static void *sockfd_add(int fd)
{
	if (!finaliser_bound) {
		atexit(close_all_sockets);
		finaliser_bound = true;
	}
	return list_insert_after(&fds, sizeof(fd), &fd);
}

static void sockfd_remove(struct list *item)
{
	list_remove(&fds, item);
}

static bool run_async(void (*func)(void *), void *closure, char *name, void *id_ignored)
{
	(void)name;
	(void)id_ignored;
	pthread_t tmp;
	return pthread_create(&tmp, NULL, (void *(*)(void *)) func, closure) == 0;
}

bool setsockopt_int(int fd, int level, int key, int value)
{
	return setsockopt(fd, level, key, &value, sizeof(value)) == 0;
}

bool setsockopt_keepalive(int fd)
{
	return setsockopt_int(fd, SOL_SOCKET, SO_KEEPALIVE, 1) &&
			setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPCNT, 6) &&
			setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPIDLE, 30) &&
			setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPINTVL, 5);
}

bool setsockopt_df(int fd)
{
	return setsockopt_int(fd, IPPROTO_IP, IP_MTU_DISCOVER, IP_PMTUDISC_DO);
}

bool setsockopt_nodelay(int fd)
{
	return setsockopt_int(fd, IPPROTO_TCP, TCP_NODELAY, 1);
}

static void socket_client_configure(struct socket_client *state)
{
	if (!setsockopt_int(state->fd, SOL_SOCKET, SO_RCVLOWAT, 1)) {
		socket_error_num("set receive low-water mark for connection", &state->addr, &state->port);
	}

	if (!setsockopt_keepalive(state->fd)) {
		socket_error_num("enable keep-alive for connection", &state->addr, &state->port);
	}
}

bool socket_client_init(struct socket_client *state, const struct fstr *addr, const struct fstr *port, socket_client_on_error *on_error)
{
	state->on_error = on_error ? on_error : (socket_client_on_error *) socket_default_on_error;
	state->l = NULL;
	fstr_init_copy(&state->addr, addr);
	fstr_init_copy(&state->port, port);
	state->fd = net_connect(&state->addr, &state->port, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, 0);
	if (state->fd == -1) {
		switch (errno) {
		case EINTR:
			break;
		default:
			socket_error_num("connect to", &state->addr, &state->port);
			goto fail;
		}
	}
	if (select_single(state->fd, sem_write | sem_error) & (sem_fail | sem_error)) {
		socket_error_num("asynchronously connect to", &state->addr, &state->port);
		goto fail;
	}
	state->l = sockfd_add(state->fd);
	socket_client_configure(state);
	return true;
fail:
	socket_client_destroy(state);
	return false;
}

void socket_client_destroy(struct socket_client *state)
{
	if (state->l != NULL) {
		sockfd_remove(state->l);
	}
	if (state->fd != -1) {
		close(state->fd);
	}
	state->fd = -1;
}

bool socket_client_send(struct socket_client *state, const void *msg, size_t length)
{
	ssize_t res = send(state->fd, msg, length, 0);
	if (res == -1) {
		socket_error_num("send to", &state->addr, &state->port);
		return false;
	}
	if ((size_t) res != length) {
		socket_error_num("completely send to", &state->addr, &state->port);
		return false;
	}
	return true;
}

ssize_t socket_client_recv_partial(struct socket_client *state, void *msg, size_t max_length, size_t min_length)
{
	setsockopt_int(state->fd, SOL_SOCKET, SO_RCVLOWAT, min_length);
	ssize_t res = recv(state->fd, msg, max_length, 0);
	setsockopt_int(state->fd, SOL_SOCKET, SO_RCVLOWAT, 1);
	if (res == -1) {
		socket_error_num("receive from", &state->addr, &state->port);
		return -1;
	}
	return res;
}

bool socket_client_recv(struct socket_client *state, void *msg, size_t length)
{
	setsockopt_int(state->fd, SOL_SOCKET, SO_RCVLOWAT, length);
	ssize_t res = recv(state->fd, msg, length, MSG_WAITALL);
	setsockopt_int(state->fd, SOL_SOCKET, SO_RCVLOWAT, 1);
	if (res == -1) {
		socket_error_num("receive from", &state->addr, &state->port);
		return false;
	}
	if ((size_t) res != length) {
		if (errno == 0 && res == 0) {
			socket_error_num("[EOF] receive from", &state->addr, &state->port);
		} else {
			socket_error_num("completely receive from", &state->addr, &state->port);
		}
		return false;
	}
	return true;
}

size_t socket_client_peek(struct socket_client *state, void *msg, size_t length)
{
	setsockopt_int(state->fd, SOL_SOCKET, SO_RCVLOWAT, length);
	ssize_t res = recv(state->fd, msg, length, MSG_WAITALL | MSG_PEEK);
	setsockopt_int(state->fd, SOL_SOCKET, SO_RCVLOWAT, 1);
	if (res == -1) {
		socket_error_num("receive from", &state->addr, &state->port);
		return 0;
	}
	return (size_t) res;
}

enum select_event_mask socket_client_select(struct socket_client *state, enum select_event_mask mask)
{
	return select_single(state->fd, mask);
}

bool socket_client_dont_fragment(struct socket_client *state)
{
	return setsockopt_df(state->fd);
}

bool socket_client_disable_nagle(struct socket_client *state)
{
	return setsockopt_nodelay(state->fd);
}

bool socket_server_init(struct socket_server *state, const struct fstr *addr, const struct fstr *port, socket_server_on_error *on_error, socket_client_on_error *on_con_error)
{
	state->on_error = on_error ? on_error : (socket_server_on_error *) socket_default_on_error;
	state->on_client_error = on_con_error ? on_con_error : (socket_client_on_error *) socket_default_on_error;
	state->l = NULL;
	fstr_init_copy(&state->addr, addr);
	fstr_init_copy(&state->port, port);
	state->fd = net_bind(&state->addr, &state->port, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, AI_PASSIVE, 0);
	if (listen(state->fd, SOCKET_SERVER_QUEUE_SIZE)) {
		switch (errno) {
		case EADDRINUSE:
			socket_error_msg("listen on", &state->addr, &state->port, "address is in use");
			goto fail;
		default:
			socket_error_num("listen on", &state->addr, &state->port);
			goto fail;
		}
	}
	state->l = sockfd_add(state->fd);
	return true;
fail:
	socket_server_destroy(state);
	return false;
}

void socket_server_destroy(struct socket_server *state)
{
	if (state->l) {
		sockfd_remove(state->l);
		state->l = NULL;
	}
	if (state->fd != -1) {
		close(state->fd);
		state->fd = -1;
	}
}

struct server_thread_closure
{
	struct socket_client client;
	server_thread *handler;
	void *closure;
};

static void server_thread_entry_point(struct server_thread_closure *closure)
{
	closure->handler(&closure->client, closure->closure);
	socket_client_destroy(&closure->client);
	free(closure);
}

bool socket_server_accept(struct socket_server *state, struct socket_client *out)
{
	struct sockaddr address;
	socklen_t addr_len = sizeof(address);
	int fd = accept(state->fd, &address, &addr_len);
	if (fd == -1) {
		return false;
	}
	out->on_error = state->on_client_error;
	out->fd = fd;
	out->l = sockfd_add(out->fd);
	fstr_init(&out->addr);
	fstr_init(&out->port);
	net_resolve_reverse(&address, addr_len, &out->addr, &out->port, 0);
	socket_client_configure(out);
	return true;
}

bool socket_server_accept_async(struct socket_server *state, server_thread *handler, void *closure)
{
	struct socket_client out;
	if (!socket_server_accept(state, &out)) {
		return false;
	}
	struct server_thread_closure *p = malloc(sizeof(*p));
	p->client = out;
	p->handler = handler;
	p->closure = closure;
	if (!run_async( (void (*)()) server_thread_entry_point, p, "TCP client server", NULL)) {
		free(p);
		return false;
	}
	return true;
}

bool close_socket_callback(int *fd)
{
	close(*fd);
	return false;
}

void close_all_sockets()
{
	list_filter(&fds, (list_predicate *) close_socket_callback);
}

#if defined TEST_ctcp
/* Echoes whatever the server sends, back to the server */

volatile bool end = false;

static void (*old_sigint)(int) = NULL;
static void (*old_sigterm)(int) = NULL;

static void sigint(int x)
{
	(void) x;
	close(STDIN_FILENO);
	signal(SIGINT, old_sigint);
}

static void sigterm(int x)
{
	(void) x;
	close(STDIN_FILENO);
	signal(SIGINT, old_sigterm);
}

int main(int argc, char *argv[])
{
	struct socket_client client;
	if (argc < 3) {
		fprintf(stderr, "Syntax: %s <addr> <port> [initial-message]\n", argv[0]);
		return 1;
	}
	struct fstr addr;
	struct fstr port;
	fstr_init_ref(&addr, argv[1]);
	fstr_init_ref(&port, argv[2]);
	if (!socket_client_init(&client, &addr, &port, NULL)) {
		fprintf(stderr, "Failed to connect to " PRIfs ":" PRIfs "\n", prifs(&addr), prifs(&port));
		return 2;
	}
	setsockopt_nodelay(client.fd);
	setsockopt_keepalive(client.fd);
	if (dup2(client.fd, STDIN_FILENO) == -1 || dup2(client.fd, STDOUT_FILENO) == -1) {
		fprintf(stderr, "Failed to redirect stdio to socket (%d)\n", errno);
		return 3;
	}
	close(client.fd);
	old_sigint = signal(SIGINT, sigint);
	old_sigterm = signal(SIGTERM, sigterm);
	size_t ignore;
	for (int i = 3; i < argc; i++) {
		char buf[4096];
		int len = snprintf(buf, sizeof(buf), "%s\n", argv[i]);
		ignore = write(STDOUT_FILENO, buf, len);
	}
	while (select_single(STDIN_FILENO, sem_read | sem_error) == sem_read) {
		char buf[4096];
		ssize_t len = read(STDIN_FILENO, buf, sizeof(buf));
		if (len == -1) {
			fprintf(stderr, "Error %d occurred\n", errno);
			break;
		}
		fprintf(stderr, "%zd bytes echoed\n", len);
		ignore = write(STDOUT_FILENO, buf, len);
	}
	(void) ignore;
	return 0;
}
#endif
