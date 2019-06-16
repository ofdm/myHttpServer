#include "Util.h"

const int LISTENQ=1024;
const int MAX_FD = 65536;

//设置文件描述符为非阻塞
int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option|O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

void addsig(int sig, void(handler)(int), bool restart)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
		sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL)!=-1);
}

int socket_bind_listen(const char* ip, int port)
{
	// 检查port值，取正确区间范围
	port = ((port <= 1024) || (port >= 65535)) ? 8888 : port;
	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd>=0);

    // 消除bind时"Address already in use"错误
	int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    
	struct linger tmp = {0, 0};
	//设置sock选项：在SOL_SOCKET层的SO_LINGER选项：若有数据待发送，则延迟关闭
	//tmp是待操作选项的值
	setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

	int ret = 0;
	struct sockaddr_in server_addr;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &server_addr.sin_addr);
	server_addr.sin_port = htons(port);

	ret = bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
	assert(ret>=0);

	ret = listen(listenfd, LISTENQ);
	assert(ret>=0);

	return listenfd;
}

void accept_connection(int fd, http_conn *users)
{
	struct sockaddr_in client_address;
	socklen_t client_addrlength = sizeof(client_address);
	//从监听fd中接受连入的客户，客户fd为connfd
	int connfd = accept(fd, (struct sockaddr*)&client_address, &client_addrlength);
	if(connfd<0)
	{
		printf("error is: %d\n", errno);
		//continue;
		return;
	}
	if(http_conn::m_user_count>=MAX_FD) //连接的客户太多了
	{
		const char *info = "Internal server busy";
		printf("%s", info);
		send(connfd, info, strlen(info), 0);  //把出现的info错误告诉客户
		close(connfd);
		//continue;
		return;
	}
	//初始化这个客户，其实就是把这个客户fd加到内核事件表里来等待这个fd有（EPOLLIN|EPOLLET|EPOLLRDHUP）事件发生
	//还把这个fd设置为非阻塞
	//另外初始化http_conn这个类中的成员变量
	users[connfd].init(connfd, client_address);
}
