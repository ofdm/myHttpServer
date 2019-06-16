#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>    //pipe，dup
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536           //最多可连入的客户数量
#define MAX_EVENT_NUMBER 10000 //epoll_wait中就绪事件最大数目

extern int addfd(int epollfd, int fd, bool one_shot);   //one_shot:一个socket连接在任一时刻只被一个线程处理
extern int removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int), bool restart=true)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
		sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL)!=-1);
}

void show_error(int connfd, const char *info)
{
	printf("%s", info);
	send(connfd, info, strlen(info), 0);  //把出现的info错误告诉客户
	close(connfd);
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

	//忽略SIGPIPE信号
	//当类型为SOCK_STREAM的套接字义不再连接时进程写该套接字会产生该SIGPIPE信号
	addsig(SIGPIPE, SIG_IGN); 

	//动态创建一个线程池
	threadpool<http_conn> *pool = NULL;
	try
	{
		pool = new threadpool<http_conn>;
	}
	catch(...)
	{
		return 1;
	}

	//动态创建大小为MAX_FD的http客户数组
	http_conn *users = new http_conn[MAX_FD];
	assert(users);
	int user_count = 0;

	//网络编程服务器端的常规写法
	//---**************************************************************
	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd>=0);
	struct linger tmp = {1, 0};
	//设置sock选项：在SOL_SOCKET层的SO_LINGER选项：若有数据待发送，则延迟关闭
	//tmp是待操作选项的值
	setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

	int ret = 0;
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);

	ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	assert(ret>=0);

	ret = listen(listenfd, 5);
	assert(ret>=0);
	//---**************************************************************


	epoll_event events[MAX_EVENT_NUMBER];   //epoll_wait中就绪事件数组
	int epollfd = epoll_create(5);
	assert(epollfd!=-1);
	//对于监听的fd不能开启one_shot，否则它只能处理一个客户连接了
	addfd(epollfd, listenfd, false);
	http_conn::m_epollfd = epollfd;

	while(true)
	{
		int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);  
		if((number<0)&&(errno!=EINTR))
		{
			printf("epoll failure\n");
			break;
		}

		//能进入这个循环说明number大于0了，也就是有就绪事件了
		for(int i=0; i<number; i++)
		{
			//拿出就绪事件的对应的文件描述符
			int sockfd = events[i].data.fd;   
			if(sockfd==listenfd)   //判断这个文件描述符是不是监听的fd
			{
				struct sockaddr_in client_address;
				socklen_t client_addrlength = sizeof(client_address);
				//从监听fd中接受连入的客户，客户fd为connfd
				int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
				if(connfd<0)
				{
					printf("error is: %d\n", errno);
					continue;
				}
				if(http_conn::m_user_count>=MAX_FD) //连接的客户太多了
				{
					show_error(connfd, "Internal server busy");
					continue;
				}
				//初始化这个客户，其实就是把这个客户fd加到内核事件表里来等待这个fd有（EPOLLIN|EPOLLET|EPOLLRDHUP）事件发生
				//还把这个fd设置为非阻塞
				//另外初始化http_conn这个类中的成员变量
				users[connfd].init(connfd, client_address);
			}
			//如果就绪事件中有EPOLLRDHUP|EPOLLHUP|EPOLLERR：对方TCP关闭|挂起（比如管道的写端被关闭）|错误发生
			else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))  
				//把这个fd从内核事件表中删除，客户数量减1，把这个fd置为-1
				users[sockfd].close_conn();
			else if(events[i].events&EPOLLIN)
			{
				if(users[sockfd].read())
					//读完了sockfd中的东西后，把它加到请求队列中
					//正常加进去后就是把信号量加1，让创建的其他线程开始work
					pool->append(users+sockfd);
				else
					users[sockfd].close_conn();
			}
			else if(events[i].events&EPOLLOUT)
			{
				if(!users[sockfd].write())
					users[sockfd].close_conn();
			}
			else{}
		}
	}
	close(epollfd);
	close(listenfd);
	delete[] users;
	delete pool;
	return 0;
}
