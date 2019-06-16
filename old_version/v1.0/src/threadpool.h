#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"

template<typename T>
class threadpool
{
public:
	threadpool(int thread_number = 8, int max_requests = 10000);
	~threadpool();
	bool append(T *request);  //往请求队列添加任务
private:
	//工作线程运行的函数，它不断从工作队列中取出任务并执行
	static void* worker(void *arg);  
	void run();

	int m_thread_number; //线程池中线程数量
	int m_max_requests; //请求队列中允许的最大请求数
	pthread_t *m_threads; //线程池数组
	std::list<T*> m_workqueue; //请求队列
	locker m_queuelocker; //保护请求队列的互斥锁
	sem m_queuestat;  //？
	bool m_stop; //创建线程池是为false，销毁线程池时被置为true
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests): m_thread_number(thread_number), m_max_requests(max_requests),
m_stop(false), m_threads(NULL)
{
	if((thread_number<=0)||(max_requests<=0))
		throw std::exception();
	m_threads = new pthread_t[m_thread_number];
	if(!m_threads)
		throw std::exception();

	for(int i=0; i<thread_number; ++i)
	{
		printf("create the %dth thread\n", i);
		if(pthread_create(m_threads+i, NULL, worker, this)!=0) //创建线程时第三个参数必须是一个静态函数
		{
			delete[] m_threads;
			throw std::exception(); 
		}
		if(pthread_detach(m_threads[i]))  
		{
			delete[] m_threads;
			throw std::exception();
		}
	}
} 

template<typename T>
threadpool<T>::~threadpool()
{
	delete[] m_threads;
	m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T *request)
{
	m_queuelocker.lock(); //因为工作队列被所有线程共享，所以操作它时要加锁
	if(m_workqueue.size()>m_max_requests)
	{
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post(); //将信号量加1，其他正在调用sem_wait等待信号量的线程将被唤醒
	return true;
}

template<typename T>
void* threadpool<T>::worker(void *arg)
{
	threadpool *pool = (threadpool*)arg;   //要在一个静态函数中使用动态成员，可以将类的对象作为参数传进来(this)，调用其动态方法
	pool->run();
	return pool;
}

template<typename T>
void threadpool<T>::run()
{
	while(!m_stop)  //while(m_stop==false)
	{
		m_queuestat.wait(); //将信号量减1，如果信号量的值为0，则它会阻塞，一开始所有创建的线程都阻塞在这，直到post
		m_queuelocker.lock();
		if(m_workqueue.empty())
		{
			m_queuelocker.unlock();
			continue;
		}
		T* request = m_workqueue.front(); //取出请求队列的头部
		m_workqueue.pop_front(); //删除队列中被取出的头部
		m_queuelocker.unlock();
		if(!request)
			continue;
		request->process();
	}
}
#endif
