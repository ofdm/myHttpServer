// @Author YY Thom
#pragma once

#include <cassert>
#include <sys/epoll.h>

#include "Util.h"
#include "http_conn.h"
#include "Threadpool.h"

class http_conn;
class threadpool;
int epoll_init();
void epoll_addfd(int epollfd, int fd, bool one_shot);
void epoll_modfd(int epollfd, int fd, int ev);
void epoll_removefd(int epollfd, int fd);
int my_epoll_wait(int epoll_fd, struct epoll_event *events, int max_events, int timeout);
void handle_events(struct epoll_event *events, int fd, int number, http_conn *users, threadpool *pool);
