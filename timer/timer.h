#ifndef MIN_HEAP
#define MIN_HEAP
#include<iostream>
#include<netinet/in.h>
#include <time.h>

using std::exception;
#define BUFFER_SIZE 64

class heap_timer;
/*bind socket and timer together*/
struct client_data{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    heap_timer *timer;
};
/*the timer class*/
class heap_timer{
public:
    time_t expire;/*absolute time for a timer to take effect*/
    void(*cb_func)(client_data*);/*call back function of timer*/
    client_data* userdata;
    int position; /*position in the heap array*/
    heap_timer(){}
    heap_timer(int delay){
        expire = time(NULL) + delay;
    }
};
class time_heap{
private:
    heap_timer **array; /*address of an array of the  pointers of heap_timers*/
    int capacity; /*capacity of heap*/
    int cur_size;/*current number of elements in heap*/
    /* ensure the hole node's subtree is a minheap*/
public:
    void percolate_down(int hole){
        heap_timer *temp = array[hole];
        int child = 0;
        for(; ((hole*2+1)<=(cur_size-1)); hole=child){
            child = hole*2+1;
            if((child<(cur_size-1)) 
            && (array[child+1]->expire < array[child]->expire)){
                ++child;
            }
            if(array[child]->expire < temp->expire){
                array[child]->position = hole;
                array[hole] = array[child];    
            }
            else break;
        }
        temp->position = hole;
        array[hole] = temp;
    }

    void resize()throw(std::exception){
        heap_timer **temp = new heap_timer*[2*capacity];
        for(int i = 0; i < 2*capacity; ++i){
            temp[i] = NULL;
        }
        if(!temp){
            throw std::exception();
        }
        capacity = 2*capacity;
        for(int i = 0; i < cur_size; ++i){
            temp[i] = array[i];
        }
        delete[] array;
        array = temp;
    }

    /*initialize an empty heap, whose size is cap*/
    time_heap(int cap)throw(std::exception):capacity(cap), cur_size(0){
        array = new heap_timer *[capacity];/*create the heap*/
        if(!array){
            throw std::exception();
        }
        for(int i = 0; i < capacity; ++i){
            array[i] = NULL;
        }
    }
    time_heap(heap_timer** init_array, int size, int capacity)throw(std::exception):cur_size(size), capacity(capacity){
        if(capacity<size){
            throw std::exception();
        }
        array = new heap_timer*[capacity];
        if(!array){
            throw std::exception();
        }
        for(int i = 0; i < capacity; ++i){
            array[i] = NULL;
        }
        if(size){/*initialize heap*/
            for(int i = 0; i < size; i++){
                array[i] = init_array[i];
            }
            for(int i = (cur_size-1)/2; i >= 0; --i){
                percolate_down(i);
            }
        }   
    }

    ~time_heap(){
        for(int i = 0; i < cur_size; ++i){
            delete array[i];
        }
        delete[] array;
    }

    void add_timer(heap_timer* timer)throw(std::exception){
        if(!timer) return;
        if(cur_size >= capacity) resize(); //twice the capacity
        /*a new elemnt is added*/
        int hole = cur_size++;
        int parent = 0;
        for(; hole > 0; hole = parent){
            parent = (hole-1)/2;
            if(array[parent]->expire <= timer->expire) break;
            array[parent]->position = hole;
            array[hole] = array[parent];
        }
        array[hole] = timer;
        timer->position = hole;
    }

    void del_timer(heap_timer* timer){
        if(!timer) return;
        timer->cb_func = NULL;
    }

    bool empty()const{return cur_size == 0;}

    heap_timer* top()const{
        if(empty()) return NULL;
        return array[0];
    }

    void pop_timer(){
        if(empty()) return;
        if(array[0]) {
            // printf("fd %d's timer  deleted\n", array[0]->userdata->sockfd);
            delete array[0];
            /*replace the original top element with the last element*/
            --cur_size;
            array[cur_size]->position = 0;
            array[0] = array[cur_size];

            percolate_down(0);  
        }
    }
    void tick(){
        heap_timer* tmp = array[0];
        time_t cur = time(NULL);/*circulately process expired timers*/
        while(!empty()){
            if(!tmp){
                break;
            }
            /*the top timer is not expired*/
            if(tmp->expire > cur){
                break;
            }
            if(array[0]->cb_func){
                array[0]->cb_func(array[0]->userdata);
            }
            pop_timer();
            tmp = array[0];
        }
    }
};

#endif