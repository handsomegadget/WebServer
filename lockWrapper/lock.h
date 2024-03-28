#ifndef LOCK_H
#define LOCK_H // prevent multiple definition

#include <exception>
#include <pthread.h>
#include <semaphore.h>
using namespace std;

class sem
{
private:
sem_t semaphore;
public:
    sem()
    {
        if (sem_init(&semaphore, 0, 0) != 0) throw exception();
    }

    /*Initiate semaphore with the value
    Also a substitution for mutual exclusion lock
    when it is initialized to 1*/
    sem(int val)
    {
        if (sem_init(&semaphore, 0, val) != 0) throw exception();
    }

    ~sem() {sem_destroy(&semaphore);}

    int wait()
    {
        return sem_wait(&semaphore) == 0;
    }
    
    int post()
    {
        return sem_post(&semaphore) == 0;
    }
};

#endif