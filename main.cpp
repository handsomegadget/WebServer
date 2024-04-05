#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include<sys/epoll.h>
#include "./lockWrapper/lock.h"
#include "./threadpool/threadpool.h"
#include "./http_parser/http_connection.h"
#include "./timer/timer.h"

#define listenfdET
// #define listenfdLT

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

// 定时器
static int pipefd[2]; // 传递信号给主线程的管道,同样被注册在epoll的事件表里
static time_heap heap_timers(10000);
static int epollfd = 0;

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot, bool isServer);
extern int setnonblocking(int fd);

//信号处理函数
void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}


//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    heap_timers.tick();
    alarm(TIMESLOT);
}

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data *user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    printf("close fd %d", user_data->sockfd);
}

void print_error_info(char* message){
    fputs(message, stderr);
    fputc('\n', stderr);

}

int main(int argc, char* argv[]){

    if(argc != 2){
        fprintf(stderr,"Usage: %s  <Port>\n", argv[0]);
        exit(1);
    }

    addsig(SIGPIPE, SIG_IGN);

    threadpool *pool = NULL;
    pool = new threadpool();

    http_conn *users_table = new http_conn[MAX_FD];
    if(users_table == NULL){
        print_error_info("users table create error\n"); 
    } 
    

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if(listenfd < 0){
        print_error_info("socket create error");
    }


    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(atoi(argv[1]));

    int flag = 1;
    int ret_value;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret_value = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    if(ret_value < 0){
        print_error_info("bind() error!");
    }
    ret_value = listen(listenfd, 5);
    if(ret_value < 0){
        print_error_info("listen() error!");
    }

    //epoll events table in the kernel
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    if(epollfd == -1) print_error_info("epoll_create() error!");

    //add server socket in epoll event table, in case the connect() request comes in
    addfd(epollfd, listenfd, false, true);
    http_conn::m_epollfd = epollfd;
    //创建管道，设置为套接字对
    ret_value = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false, true);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;

    client_data* users_timer = new client_data[MAX_FD];
    bool timeout = false;
    alarm(TIMESLOT);

    //main loop start here
    while(!stop_server){
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if(number < 0 && errno != EINTR){
            printf("errno: %d\n", errno);
            print_error_info("Epoll_wait() failure!");
            exit(1);
        }
        for(int i = 0; i < number; ++i){
            int sockfd = events[i].data.fd;

            //It's the connect() request from clients!
            if(sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);

#ifdef listenfdLT
                int clientfd = accept(listenfd, (struct sockaddr *)&client_address, &client_address_len);
                if(clientfd < 0){
                    print_error_info("accept() error ");
                    continue;
                }
                
                if(http_conn::m_user_count >= MAX_FD){
                    print_error_info("Internet server busy");
                    continue;
                }
                users_table[clientfd].init(clientfd, client_address);

                users_timer[clientfd].address = client_address;
                users_timer[clientfd].sockfd = clientfd;
                heap_timer *timer = new heap_timer;
                timer->userdata = &users_timer[clientfd];
                timer->cb_func = cb_func;
                time_t cur = time(0);
                timer->expire = cur + 3*TIMESLOT;
                users_timer[clientfd].timer = timer;
                heap_timers.add_timer(timer);
                
#endif
#ifdef listenfdET
                while(true){
                    int clientfd = accept(listenfd, (struct sockaddr *)&client_address, &client_address_len);
                    if(clientfd < 0){
                        print_error_info("accept() error ");
                        break;
                    }
                
                    if(http_conn::m_user_count >= MAX_FD){
                        print_error_info("Internet server busy");
                        break;
                    }
                    users_table[clientfd].init(clientfd, client_address);
                    users_timer[clientfd].address = client_address;
                    users_timer[clientfd].sockfd = clientfd;
                    heap_timer *timer = new heap_timer;
                    timer->userdata = &users_timer[clientfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(0);
                    timer->expire = cur + 3*TIMESLOT;
                    users_timer[clientfd].timer = timer;
                    heap_timers.add_timer(timer);                                    
                }
#endif
            }
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret_value = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret_value == -1 || ret_value == 0)
                {continue;}
                else
                {
                    for (int i = 0; i < ret_value; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                printf("Server deliberately closed the connection of  %d\n", sockfd);
                epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
                close(sockfd);
                http_conn::m_user_count--;
            }
            
            // ready-for-read events
            else if(events[i].events & EPOLLIN){
                heap_timer* timer = users_timer[sockfd].timer;
                if(users_table[sockfd].read_once()){
                    //put the event in the request queue
                    pool->append(users_table + sockfd);
                    printf("start read request from fd %d\n", sockfd);
                    if(timer){
                        time_t cur = time(0);
                        timer->expire = cur+3*TIMESLOT;
                        printf("adjust timer %d once\n", sockfd);
                        heap_timers.percolate_down(timer->position);
                    }
                }
                else{
                    timer->cb_func(&users_timer[sockfd]);
                    if(timer){
                        heap_timers.del_timer(timer);
                    }
                }
            }
            //ready-for-write event
            else if (events[i].events & EPOLLOUT)
            {              
                heap_timer *timer = users_timer[sockfd].timer;
                if (users_table[sockfd].write())
                {
                    // printf("send data to the client(%s)", inet_ntoa(users_table[sockfd].get_address()->sin_addr));
                    if(timer){
                        time_t cur = time(0);
                        timer->expire = cur+3*TIMESLOT;
                        printf("adjust timer %d once\n", sockfd);
                        heap_timers.percolate_down(timer->position);
                    }
                }
                else
                {   
                    timer->cb_func(&users_timer[sockfd]);
                    if(timer){
                        heap_timers.del_timer(timer);
                    }
                }
            }
            if(timeout){
                timer_handler();
                timeout = false;
            }
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users_timer;
    delete[] users_table;
    delete pool;
    return 0;

}