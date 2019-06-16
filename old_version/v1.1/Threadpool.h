// @Author YY Thom
#pragma once
#include <pthread.h>
#include <list>
#include <cstdio>
#include <exception>

#include "http_conn.h"

class http_conn;

class threadpool
{
public:
	threadpool(int thread_number = 4, int max_requests = 10000);
	~threadpool();
	bool append(http_conn *request);  //往请求队列添加任务
private:
	//工作线程运行的函数，它不断从工作队列中取出任务并执行
	static void* worker(void *arg);  
	void run();

	int m_thread_number; //线程池中线程数量
	int m_max_requests; //请求队列中允许的最大请求数
	pthread_t *m_threads; //线程池数组
	std::list<http_conn*> m_workqueue; //请求队列
	static pthread_mutex_t lock; //保护请求队列的互斥锁
	static pthread_cond_t notify;  //条件变量，控制线程同步
	bool m_stop; //创建线程池是为false，销毁线程池时被置为true
};
