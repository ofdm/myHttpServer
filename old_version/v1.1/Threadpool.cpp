#include "Threadpool.h"

pthread_mutex_t threadpool::lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t threadpool::notify = PTHREAD_COND_INITIALIZER;

threadpool::threadpool(int thread_number, int max_requests): m_thread_number(thread_number), m_max_requests(max_requests),
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

threadpool::~threadpool()
{
	delete[] m_threads;
	pthread_mutex_destroy(&lock);
	pthread_cond_destroy(&notify);
	m_stop = true;
}

bool threadpool::append(http_conn *request)
{
	pthread_mutex_lock(&lock); //因为工作队列被所有线程共享，所以操作它时要加锁
	if(m_workqueue.size()>m_max_requests)
	{
		pthread_mutex_unlock(&lock);
		return false;
	}
	m_workqueue.push_back(request);
	pthread_cond_signal(&notify);  //唤醒线程
	pthread_mutex_unlock(&lock);
	return true;
}

void* threadpool::worker(void *arg)
{
	threadpool *pool = (threadpool*)arg;   //要在一个静态函数中使用动态成员，可以将类的对象作为参数传进来(this)，调用其动态方法
	pool->run();
	return pool;
}

void threadpool::run()
{
	while(!m_stop)  //while(m_stop==false)
	{
		pthread_mutex_lock(&lock);
		if(m_workqueue.empty())
		{
			pthread_cond_wait(&notify, &lock);
			//continue;
		}
		http_conn* request = m_workqueue.front(); //取出请求队列的头部
		m_workqueue.pop_front(); //删除队列中被取出的头部
		pthread_mutex_unlock(&lock);
		if(!request)
			continue;
		request->process();
	}
}
