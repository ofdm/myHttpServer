#include "Epoll.h"

const static int LISTENQ=1024;
const int TIMESLOT = 10; 

int epoll_init()
{
	int epollfd = epoll_create(LISTENQ);
	assert(epollfd!=-1);
    return epollfd;
}

void epoll_addfd(int epollfd, int fd, bool one_shot)
{
	epoll_event event;
	event.data.fd = fd;
	//EPOLLIN：数据可读；EPOLLET：边沿触发（对于相同事件只通知一次，所以要立即处理，比较高效）；
	//EPOLLRDHUP：TCP连接被对方关闭或者对方关闭了写操作
	event.events = EPOLLIN|EPOLLET|EPOLLRDHUP;
	if(one_shot)
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	//把fd设置非阻塞
	setnonblocking(fd);
}

void epoll_modfd(int epollfd, int fd, int ev)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;//rdhup
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void epoll_removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

int my_epoll_wait(int epoll_fd, struct epoll_event *events, int max_events, int timeout)
{
	int number = epoll_wait(epoll_fd, events, max_events, timeout); 
	return number;
}

void handle_events(struct epoll_event *events, int fd, int number, http_conn *users, threadpool *pool, time_heap *timer_heap, int *pipefd,  bool *timeout)
{
	for(int i=0; i<number; i++)
		{
			//拿出就绪事件的对应的文件描述符
			int sockfd = events[i].data.fd;   
			if(sockfd==fd)   //判断这个文件描述符是不是监听的fd
			{
				accept_connection(fd, users, timer_heap);
			}
			//如果就绪事件中有EPOLLRDHUP|EPOLLHUP|EPOLLERR：对方TCP关闭|挂起（比如管道的写端被关闭）|错误发生
			else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))
			{  
				timer_heap->pop_timer();
				//把这个fd从内核事件表中删除，客户数量减1，把这个fd置为-1
				users[sockfd].close_conn();
			}
			//处理管道来的信号
			else if((sockfd==pipefd[0]) && (events[i].events&EPOLLIN))
			{
					int sig;
					char signals[1024];
					int ret = recv(pipefd[0], signals, sizeof(signals), 0);
					if(ret==-1||ret==0)
					{
						continue;
					}
					else
					{
						for(int i=0;i<ret;++i)
						{
							switch(signals[i])
							{
							case SIGALRM:
							{	
								*timeout = true;
								break;
							}
							case SIGTERM:
							{

							}			
							}
						}
					}
			}
			else if(events[i].events&EPOLLIN)
			{
				heap_timer* timer = users[sockfd].timer;
				if(users[sockfd].read())
				{
					//读完了sockfd中的东西后，把它加到请求队列中
					//正常加进去后就是把信号量加1，让创建的其他线程开始work
					pool->append(users+sockfd);
					if(timer)
                    {
                        time_t cur = time( NULL );
                        timer->expire = cur +3*TIMESLOT;
                        printf( "adjust timer once\n" );
                        timer_heap->adjust_timer();
                    }

				}
				else
				{
					timer_heap->pop_timer();
					users[sockfd].close_conn();
				}
			}
			else if(events[i].events&EPOLLOUT)
			{
				if(!users[sockfd].write())
				{
					timer_heap->pop_timer();
					users[sockfd].close_conn();
				}
			}
			else{}
		}
}
