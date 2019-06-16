// @Author YY Thom
#pragma once
#include <netinet/in.h>
#include <time.h>
#include "http_conn.h"
class http_conn;

class heap_timer
{
public:
    heap_timer( int delay )
    {
        expire = time( NULL ) + delay;
    }

public:
   time_t expire;
   void (*cb_func)( http_conn* );
   http_conn* user_data;
};

class time_heap
{
public:
    time_heap(int cap);
    time_heap(heap_timer** init_array, int size, int capacity);
	~time_heap();

public:
    void add_timer( heap_timer* timer ); 
    void adjust_timer();
    void del_timer( heap_timer* timer );
    heap_timer* top() const;
    void pop_timer();

    void tick();
    bool empty() const { return cur_size == 0; }

private:
    void percolate_down( int hole );
	void resize();


private:
    heap_timer** array;
    int capacity;
    int cur_size;
};