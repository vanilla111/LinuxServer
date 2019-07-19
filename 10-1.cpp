#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

#define MAX_EVENT_NUMBER 1024
static int pipefd[2];

/**
 * 信号是一种异步事件：信号处理函数和程序的主循环是两条不同的执行路线。
 * 信号处理函数需要尽可能快的执行完毕，以确保信号不被屏蔽太久。
 * 典型解决方案是：信号的主要处理逻辑放到程序的主循环中，当信号处理函数被触发时，
 * 他只是简单地通知主循环程序接收到的信号，并把信号值传递给主循环，主循环根据接收到的信号值执行对应逻辑代码
 */

int setnonblocking(int fd)
{
	int old_opt = fcntl(fd, F_GETFL);
	int new_opt = old_opt | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_opt);
	return old_opt;
}

void addfd(int epollfd, int fd)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

void sig_handler(int sig)
{
	/* 保留errno,最后恢复，保证函数可重入性 */
	int save_errno = errno;
	int msg = sig;
	send(pipefd[1], (char*)&msg, 1, 0); /* 信号写入管道，通知主循环 */
	errno = save_errno;
}

/* 信号处理函数 */
void addsig(int sig)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
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
	/* 创建TCP socket */
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
	addfd(epollfd, listenfd);

	/* 注册pipefd[0]上的可读事件 */
	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
	assert(ret != -1);
	setnonblocking(pipefd[1]);
	addfd(epollfd, pipefd[0]);

	addsig(SIGHUP);
	addsig(SIGCHLD);
	addsig(SIGTERM);
	addsig(SIGINT);

	bool stop_server = false;

	while(!stop_server)
	{
		int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if (number < 0 && (errno != EINTR))
		{
			printf("epoll failure\n");
			break;
		}
		for (int i = 0; i < number; ++i)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd)
			{
				struct sockaddr_in client_address;
				socklen_t client_addlength = sizeof(client_address);
				int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addlength);
				addfd(epollfd, connfd);
			}
			else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
			{
				int sig;
				char signals[1024];
				ret = recv(pipefd[0], signals, sizeof(signals), 0);
				if (ret == -1) continue;
				else if (ret == 0) continue;
				else
				{
					/* 每个信号占一个字节，所以按字节逐个接收信号 */
					for (int i = 0; i < ret; ++i)
					{
						switch(signals[i])
						{
							case SIGCHLD:
							case SIGHUP: continue;
							case SIGTERM:
							case SIGINT: stop_server = true;
						}
					}
				}
			}
			else {}
		}
	}

	printf("close fds\n");
	close(listenfd);
	close(pipefd[1]);
	close(pipefd[0]);


	return 0;
}