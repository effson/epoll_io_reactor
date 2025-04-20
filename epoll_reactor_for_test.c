#include <sys/socket.h>   
#include <netinet/in.h>
#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/select.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/time.h>

#define __FD_SETSIZE 1024
#define BUFFER_LENGTH 1024

#define ENABLE_HTTP_RESPONSE 1

typedef int (*RCALLBACK)(int fd);

int receive_cb(int fd);
int accept_cb(int fd);
int send_cb(int fd);

struct conn_item{
	int fd;

	char rbuffer[BUFFER_LENGTH];
	int rlen;
	char wbuffer[BUFFER_LENGTH];
	int wlen;

	union{
		RCALLBACK accept_callback;
		RCALLBACK receive_callback;
	}receive_t;
	RCALLBACK send_callback;
};

int epfd = 0;

struct conn_item conn_list[1048576] = {0};
struct timeval jeff;

#if ENABLE_HTTP_RESPONSE
typedef struct conn_item connection_t;

int http_response(connection_t* conn){
	conn->wlen = sprintf(conn->wbuffer,
		"HTTP/1.1 200 OK\r\n"
		"Accept-Ranges: bytes\r\n"
		"Content-Length: 82\r\n"
		"Content-Type: text/html\r\n"
		"Date: Sat, 06 Aug 2022 13:16:46 GMT\r\n\r\n"
		"<html><head><title>0voice.jeff</title></head><body><h1>jeff</h1></body></html>\r\n\r\n");
	
	return conn->wlen;
}

#endif

#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

int set_event(int fd, int event, int flag){
	if (flag){ //1.add 0.mod
		struct epoll_event ev;
		ev.events = event;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	}else{
		struct epoll_event ev;
		ev.events = event;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	}
}


int accept_cb(int fd){
	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);
	
	int clientfd = accept(fd, (struct sockaddr*)&client_addr, &len);
	if(clientfd < 0){
		return -1;
	}
	printf("accept\n");
	
	set_event(clientfd, EPOLLIN, 1);

	conn_list[clientfd].fd = clientfd;
	memset(conn_list[clientfd].rbuffer, 0, BUFFER_LENGTH);
	conn_list[clientfd].rlen = 0;
	memset(conn_list[clientfd].wbuffer, 0, BUFFER_LENGTH);
	conn_list[clientfd].wlen = 0;
	conn_list[clientfd].receive_t.receive_callback = receive_cb;
	conn_list[clientfd].send_callback = send_cb;

	if ((clientfd % 1000) == 999){
		struct timeval tv_cur;
		gettimeofday(&tv_cur, NULL);
		int time_used = TIME_SUB_MS(tv_cur, jeff);

		memcpy(&jeff, &tv_cur, sizeof(struct timeval));

		printf("clientfd : %d\n", clientfd, time_used);
	}
				
	return clientfd;
}

int receive_cb(int fd){
	char *buffer = conn_list[fd].rbuffer;
	int idx = conn_list[fd].rlen;
	printf("receive\n");	
	int size = recv(fd, buffer, BUFFER_LENGTH - idx, 0);
	if (size == 0) {
		printf("disconnect\n");
		epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
		
		close(fd);
		return -1;
	}
	conn_list[fd].rlen += size;

#if 1
	memcpy(conn_list[fd].wbuffer, conn_list[fd].rbuffer, conn_list[fd].rlen);
	conn_list[fd].wlen = conn_list[fd].rlen;
	conn_list[fd].rlen -= conn_list[fd].rlen;
#else
	// http_response(&conn_list[fd]);

#endif
	set_event(fd, EPOLLOUT, 0);

	return size;
}

int send_cb(int fd){
	char *buffer = conn_list[fd].wbuffer;
	int idx = conn_list[fd].wlen;

	int size = send(fd, buffer, idx, 0);

	set_event(fd, EPOLLIN, 0);

	return size;
}

int init_server(unsigned short port){
	int sockfd = socket(AF_INET,SOCK_STREAM, 0);
	
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr_in));
	
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);
	
	if(bind(sockfd,(struct sockaddr*)&server_addr, sizeof(struct sockaddr)) == -1){
		perror("bind");
		return -1;
	}
	
	listen(sockfd, 10);
	return sockfd;
}

int main(){

	int port_count = 20;
	unsigned short port = 2048;
	int i = 0;

	epfd = epoll_create(1); // int size

	
	for (i = 0, i < port_count; i ++) {
		int sockfd = init_server(port + i);
		conn_list[sockfd].fd = sockfd;
		conn_list[sockfd].receive_t.accept_callback = accept_cb;
		set_event(sockfd, EPOLLIN, 1);
	}

	gettimeofday(&jeff, NULL);

	struct epoll_event events[1024] = {0};
	while (1) {
		int nready = epoll_wait(epfd, events, 1024, -1);

		int i = 0;
		for (i = 0; i < nready;i ++) {
			int connfd = events[i].data.fd;
			if (events[i].events & EPOLLIN) {	
				
				int size = conn_list[connfd].receive_t.receive_callback(connfd);
				//printf("receive size : %d <---- buffer : %s\n", size, conn_list[connfd].rbuffer);
			
			}
			else if (events[i].events & EPOLLOUT) {		
				
				//printf("send ----> buffer : %s\n", conn_list[connfd].wbuffer);
				int size = conn_list[connfd].send_callback(connfd);
			
			}
		}
	}

	getchar();
	// close(clientfd);
}

