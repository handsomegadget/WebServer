#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../http_parser/http_connection.h"
using namespace std;

class threadpool
{
private:
    int my_thread_num;          
    int max_request_num;
    pthread_t *my_threads;
    list<http_conn *> my_request_queue;
    sem request_queue_lock;               //initialize to 1
    sem request_number;
    bool my_stop_thread;

    static void* worker(void *arg);
    void run();

public:
    threadpool(int thread_number = 16, int max_requests = 10000);
    ~threadpool();
    bool append(http_conn *request);
};

threadpool::threadpool(int thread_number, int max_requests):request_queue_lock(1), request_number(0), my_thread_num(thread_number), max_request_num(max_requests), my_stop_thread(false), my_threads(NULL)
{
    if(thread_number <= 0 || max_requests <= 0) throw exception();
    if(!(my_threads = new pthread_t[my_thread_num])) throw exception();
    

    for(int i = 0; i < thread_number; i++){
        if(pthread_create(my_threads+i, NULL, worker, this) != 0)
        {
            delete[] my_threads;
            throw exception();
        }
        if(pthread_detach(my_threads[i]))
        {
            delete[] my_threads;
            throw exception();            
        }
    }
}

threadpool::~threadpool()
{
    delete[] my_threads;
    my_stop_thread = true;
}

bool
threadpool::append(http_conn *request)
{
    request_queue_lock.wait();
    if(my_request_queue.size() > max_request_num)
    {
        request_queue_lock.post();
        return false;
    }
    my_request_queue.push_back(request);
    request_queue_lock.post();
    request_number.post();
    return true;
}

void *
threadpool::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

void
threadpool::run()
{
    while(!my_stop_thread)
    {
        request_number.wait();
        request_queue_lock.wait();
        if(my_request_queue.empty())
        {
            request_queue_lock.post();
            continue;
        }
        http_conn *request = my_request_queue.front();
        my_request_queue.pop_front();
        request_queue_lock.post();
        if(!request) continue;
        request->process();
    }
}

#endif