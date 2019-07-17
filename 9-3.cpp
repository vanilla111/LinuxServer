#include <sys/types.h>
#include <sys/socket.h>
#include <netinet.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 10

/**
 * epoll对文件描述符有两种操作模式：LT（LevelTrigger电平触发）和ET（EdgeTrigger）。
 * LT是默认的模式。对于LT模式，epoll_wait监测到其上有事件发生并通知应用程序后，应用程序可以不立即处理该事件
 * 这样，应用程序下一次调用时仍会再次通知。而ET模式通知时必须立即处理，否则之后不再通知。
 * 所以，ET模式降低了同一个事件被重复触发的次数，因此效率比LT模式高。
 */

/**
 * 将文件描述符设置成非阻塞的
 * @param  fd 文件描述符
 * @return old_opt 原配置
 */
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
void addfd(int epollfd, int fd, bool enable_et)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN;
	if (enable_et)
	{
		event.events |= EPOLLET;
	}
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

/* LT 模式工作流程 */
void lt(epoll_event* events, int number, int epollfd, int listenfd)
{
	char buf[BUFFER_SIZE];
	for (int i = 0; i < number; ++i)
	{
		int sockfd = events[i].data.fd;
		if (sockfd == listenfd)
		{
			struct sockaddr_in client_address;
			socklen_t client_addrlength = sizeof(client_address);
			int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
			addfd(epollfd, connfd, false);
		}
		else if (events[i].events & EPOLLIN)
		{
			/* 只要socket读缓存还有未读数据，这段代码将被执行 */
			printf("event trigger once\n");
			memset(buf, '\0', BUFFER_SIZE);
			int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
			if (ret <= 0)
			{
				close(sockfd);
				continue;
			}
			printf("get %d bytes of content: %s\n", ret, buf);
		}
		else
		{
			printf("something else happened\n");
		}
	}
}

void et(epoll_event* events, int number, int epollfd, int listenfd)
{
	char buf[BUFFER_SIZE];
	for (int i = 0; i < number; ++i)
	{
		int sockfd = events[i].data.fd;
		if (sockfd == listenfd)
		{
			struct sockaddr_in client_address;
			socklen_t client_addrlength = sizeof(client_address);
			int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
			addfd(epollfd, connfd, true);
		}
		else if (events[i].events & EPOLLIN)
		{
			/* 这段代码不会被循环触发 */
			printf("event trigger once\n");
			while (1)
			{
				memset(buf, '\0', BUFFER_SIZE);
				int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
				if (ret < 0)
				{
					/* 对于非阻塞I/O，下面条件成立表示数据已经被全部读取完毕。
					此后，epoll就能再次触发socked上的EPOLLIN事件，以驱动下一次读操作 */
					if (errno == EAGAIN || (errno == EWOULDBLOCK))
					{
						printf("read later\n");
						break;
					}
					close(sockfd);
					break;
				}
				else if (ret == 0) close(sockfd);
				else printf("get %d bytes of content: %s\n", ret, buf);
			}
		}
		else 
		{
			printf("something else happened\n");
		}
	}
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
	addfd(epollfd, listenfd, true);

	while(1)
	{
		int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, - 1);
		if (ret < 0)
		{
			printf("epoll failure\n");
			break;
		}

		lt(events, ret, epollfd, listenfd);
		// et(events, ret, epollfd, listenfd);
	}

	close(listenfd);
	return 0;
}

