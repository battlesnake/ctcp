#pragma once
#include <cstd/std.h>
#include <cstd/unix.h>
#include <fixedstr/fixedstr.h>
#include "select.h"

#if !defined SOCKET_SERVER_QUEUE_SIZE
#define SOCKET_SERVER_QUEUE_SIZE 30
#endif

struct socket_client;
struct socket_server;

typedef void socket_client_on_error(struct socket_client *state, const char *func, const char *msg);

typedef void socket_server_on_error(struct socket_server *state, const char *func, const char *msg);

extern void socket_default_on_error(void *self, const char *func, const char *msg);
extern socket_client_on_error *socket_client_default_error_handler;
extern socket_server_on_error *socket_server_default_error_handler;

struct socket_client
{
	socket_client_on_error *on_error; // must be first
	int fd;
	struct fstr addr;
	struct fstr port;
	void *l;
};

struct socket_server
{
	socket_server_on_error *on_error; // must be first
	socket_client_on_error *on_client_error;
	int fd;
	struct fstr addr;
	struct fstr port;
	/* FD list pointer */
	void *l;
};

/* Client */

bool socket_client_init(struct socket_client *state, const struct fstr *addr, const struct fstr *port, socket_client_on_error *on_error);
void socket_client_destroy(struct socket_client *state);
bool socket_client_send(struct socket_client *state, const void *msg, size_t length);
ssize_t socket_client_recv_partial(struct socket_client *state, void *msg, size_t max_length, size_t min_length);
bool socket_client_recv(struct socket_client *state, void *msg, size_t length);
size_t socket_client_peek(struct socket_client *state, void *msg, size_t length);
enum select_event_mask socket_client_select(struct socket_client *state, enum select_event_mask mask);
bool socket_client_dont_fragment(struct socket_client *state);
bool socket_client_disable_nagle(struct socket_client *state);

/* Server */

typedef void server_thread(struct socket_client *state, void *closure);

uint8_t *socket_server_addr_any();

bool socket_server_init(struct socket_server *state, const struct fstr *addr, const struct fstr *port, socket_server_on_error *on_error, socket_client_on_error *on_con_error);
void socket_server_destroy(struct socket_server *state);
bool socket_server_accept(struct socket_server *state, struct socket_client *out);
bool socket_server_accept_async(struct socket_server *state, server_thread *handler, void *closure);

void close_all_sockets();

/* Utility */
bool setsockopt_int(int fd, int level, int key, int value);
bool setsockopt_keepalive(int fd);
bool setsockopt_df(int fd);
bool setsockopt_nodelay(int fd);
