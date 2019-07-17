#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>

/**
 * 即使使用ET模式，一个socket上的某个事件还是可能被触发多次。如果是并发程序，比如多个线程的情况下，
 * 一个线程读取完某个socket上的数据后开始处理，而在处理过程中该socket中又有新的数据可读，
 * 则EPOLLIN事件再次被触发，此时另一个线程被唤醒来读取这些新的数据，于是出现了两个线程同时操作一个socket的局面。
 * 我们期望任一时刻一个socket只能被一个线程处理，可以使用EPOLLONESHOT事件实现。
 * 对于注册了EPOLLONESHOT事件的文件描述符，操作系统最多触发其上注册的一个可读、可写或者异常事件，
 * 切触发一次，除非使用epoll_ctl函数重置该文件描述符上注册的EPOLLONESHOT事件。
 * 这样，当一个线程处理某个socket时，其他线程不可能有机会操作该socket。
 * 反过来思考，注册了该事件的socket一旦被某个线程处理完毕，该线应该立即重置这个socket上的EPOLLONESHOT事件，
 * 以确保这个socket下一次可读时，其EPOLLIN事件能被触发，进而让其他工作线程有机会处理这个socket。
 */

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 1024
struct fds
{
	int epollfd;
	int sockfd;
};

int setnonblocking(int fd)
{
	int old_opt = fcntl(fd, F_GETFL);
	int new_opt = old_opt | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_opt);
	return old_opt;
}

/**
 * 将文件描述符fd上的EPOLLIN注册到epollfd标识的内核事件表中
 * @param epollfd   标识内核事件表
 * @param fd        需要操作的文件描述符
 * @param enable_et 是否开启ET模式
 */
void addfd(int epollfd, int fd, bool oneshot)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	if (oneshot)
	{
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

/**
 * 重置fd上的事件。操作之后，尽管fd上的ONESHOT事件被注册
 * 但是操作系统仍然会触发fd上的EPOLLIN事件，且触发一次
 */
void reset_oneshot(int epollfd, int fd)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* 工作线程 */
void* worker(void* arg)
{
	int sockfd = ((fds*)arg)->sockfd;
	int epollfd = ((fds*)arg)->epollfd;
	printf("start new thread to receive data on fd: %d\n", sockfd);
	char buf[BUFFER_SIZE];
	memset(buf, '\0', BUFFER_SIZE);
	while(1) //循环读取，知道遇到EAGAIN错误
	{
		int ret = recv(sockfd, buf, BUFFER_SIZE-1, 0);
		if (ret == 0)
		{
			close(sockfd);
			printf("foreiner closed the connection\n");
			break;
		}
		else if(ret < 0)
		{
			if (errno == EAGAIN)
			{
				reset_oneshot(epollfd, sockfd);
				printf("read later\n");
				break;
			}
		}
		else
		{
			printf("get content : %s\n", buf);
			sleep(5);
		}
	}
	printf("end thread receiving data on fd: %d\n", sockfd);
}

int main(int argc, char const *argv[])
{
	const char *test_ip = "172.20.157.22";
	if (argc <= 2)
	{
		printf("usage: %s ip port\n", basename(argv[0]));
		return 1;
	}

	const char* ip = argv[1];
	if (strcmp(ip, "0.0.0.0") == 0)
	{
		ip = test_ip;
	}
	int port = atoi(argv[2]);
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);
	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);
	int reuse = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	assert(ret != -1);
	ret = listen(listenfd, 5);
	assert(ret != -1);

	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);
	assert(epollfd != -1);
	/* 注意：监听socket 的 listenfd上是不能注册EPOLLONESHOT事件的 */
	/* 否则应用程序只能处理一个客户端连接！
	因为后续的客户端连接请求将不再触发listenfd上的EPOLLIN事件 */
	addfd(epollfd, listenfd, false);

	while(1) 
	{
		int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if (ret < 0)
		{
			printf("epoll failure\n");
			break;
		}
		for (int i = 0; i < ret; ++i)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd)
			{
				struct sockaddr_in client_address;
				socklen_t client_addrlength = sizeof(client_address);
				int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
				/* 非监听文件描述符都注册EPOLLONTSHOT事件 */
				addfd(epollfd, connfd, true);
			}
			else if (events[i].events & EPOLLIN)
			{
				pthread_t thread;
				fds fds_for_new_worker;
				fds_for_new_worker.epollfd = epollfd;
				fds_for_new_worker.sockfd = sockfd;
				pthread_create(&thread, NULL, worker, (void*)&fds_for_new_worker);
			}
			else
			{
				printf("something else happened\n");
			}
		}
	}

	close(listenfd);
	return 0;
}

