// @Author YY Thom
#pragma once
#include <fcntl.h>
#include <cassert>

#include "http_conn.h"

class http_conn;

int setnonblocking(int fd);
void addsig(int sig, void(handler)(int), bool restart=true);

int socket_bind_listen(const char* ip, int port);
void accept_connection(int fd, http_conn *users);
