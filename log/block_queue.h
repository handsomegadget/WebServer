#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lockWrapper/lock.h"
#include "../lockfreequeue/lockfreeq.h"
using namespace std;
template <class T>
class block_queue
{
private:
    sem mutex = sem(1);
    sem s_size = sem(0);

    T *array;
    int size;
    int max_size;
    int front;
    int rear;

public:
    block_queue(int maxsize = 1000){
        if(maxsize <= 0) exit(1);
        max_size = maxsize;
        array = new T[max_size];
        size = 0;
        front = -1;
        rear = -1;
        
    }
    ~block_queue(){
        mutex.wait();
        if(array != NULL) delete[] array;

        mutex.post();
    }

    void clear(){
        mutex.wait();
        size = 0;
        front = -1;
        rear = -1;
        mutex.post();
    }

    bool isfull(){
        mutex.wait();
        if(size >= max_size){
            mutex.post();
            return true;
        }
        mutex.post();
        return false;
    }

    bool isempty(){
        mutex.wait();
        if(size == 0){
            mutex.post();
            return true;
        }
        else{
            mutex.post();
            return false;
        }
    }
    bool getfront(T &value){
        mutex.wait();
        if(size == 0){
            mutex.post();
            return false;
        }
        value = array[front];
        mutex.post();
        return true;
    }
    
    bool back(T &value){
        mutex.wait();
        if(size == 0){
            mutex.post();
            return false;
        }
        value = array[rear];
        mutex.post();
        return true;
    }

    int getsize(){
        return size;
    }

    int getmaxsize(){
        return max_size;
    }

    bool push(const T& item){
        mutex.wait();
        if(max_size <= size){
            mutex.post();
            return false;
        }

        rear = (rear+1)%max_size;
        array[rear] = item;

        size++;
        s_size.post();
        mutex.post();
        return true;
    }

    bool pop(T &item){
        s_size.wait();
        mutex.wait();
        
        front = (front+1)%max_size;
        item = array[front];
        size--;
        mutex.post();
        return true;
    }

    

};

#endif