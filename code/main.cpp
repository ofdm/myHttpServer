#include "Util.h"
#include "Epoll.h" 
#include "Threadpool.h"
#include "http_conn.h"
#include "Timer.h"

const int MAX_FD = 65536;           //最多可连入的客户数量
const int MAX_EVENT_NUMBER = 10000; //epoll_wait中就绪事件最大数目
const int TIMESLOT = 10;             //定时时间
static int pipefd[2];
static time_heap* timer_heap = new time_heap(1);

void sig_handler(int sig)
{
	int save_errno = errno;
	int msg = sig;
	send(pipefd[1], (char*)&msg, 1, 0);
	errno = save_errno;
}


int main(int argc, char *argv[])
{
	if(argc<=2)
	{
		printf("Usage: %s ip_address port_number\n", basename(argv[0]));
		return 1;
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);
	//int thread_number = atoi(argv[3]);

	//忽略SIGPIPE信号
	//当类型为SOCK_STREAM的套接字义不再连接时进程写该套接字会产生该SIGPIPE信号
	//addsig(SIGPIPE, SIG_IGN); 

	//动态创建一个线程池
	threadpool *pool = new threadpool;

	//动态创建大小为MAX_FD的http客户数组
	http_conn *users = new http_conn[MAX_FD];
	assert(users);
	int user_count = 0;

	int listenfd = socket_bind_listen(ip, port);
	assert(listenfd>=0);
	epoll_event events[MAX_EVENT_NUMBER];   //epoll_wait中就绪事件数组
	int epollfd = epoll_init();
	epoll_addfd(epollfd, listenfd, false);

	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
	assert(ret!=-1);
	setnonblocking(pipefd[1]);
	epoll_addfd(epollfd, pipefd[0], false);

	//设置信号处理函数
	addsig(SIGALRM, sig_handler);
	addsig(SIGTERM, sig_handler);
	bool timeout = false;
	alarm(TIMESLOT);
	http_conn::m_epollfd = epollfd;
	while(true)
	{
		int number = my_epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if((number<0)&&(errno!=EINTR))
		{
			printf("epoll failure\n");
			break;
		}
		handle_events(events, listenfd, number, users, pool, timer_heap, pipefd, &timeout);
		if(timeout)
		{	
			timer_heap->tick();
			alarm(TIMESLOT);
			timeout = false;
		}
	}
	close(epollfd);
	close(listenfd);
	close(pipefd[1]);
	close(pipefd[0]);
	delete[] users;
	delete pool;
	return 0;
}
