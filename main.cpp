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

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数

static int epollfd = 0;
//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int setnonblocking(int fd);

void print_error_info(char* message){
    fputs(message, stderr);
    fputc('\n', stderr);

}

int main(int argc, char* argv[]){

    if(argc != 2){
        fprintf(stderr,"Usage: %s  <Port>\n", argv[0]);
        exit(1);
    }

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
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    

    bool stop_server = false;


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

            //It's request from clients!
            if(sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);

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
                if(users_table[sockfd].read_once()){
                    //put the event in the request queue
                    pool->append(users_table + sockfd);
                    printf("start read request from fd %d\n", sockfd);
                }
                else{
                    printf("No request read from fd %d\n", sockfd);
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                    http_conn::m_user_count--;
                }
            }
            //ready-for-write event
            else if (events[i].events & EPOLLOUT)
            {              
                if (users_table[sockfd].write())
                {
                    printf("send data to the client(%s)", inet_ntoa(users_table[sockfd].get_address()->sin_addr));
                }
                else
                {   
                    printf("fail to write to client %d\n", sockfd);
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                    http_conn::m_user_count--;
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete[] users_table;
    delete pool;
    return 0;

}