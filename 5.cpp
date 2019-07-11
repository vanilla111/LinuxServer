#include <sys/un.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#define true 1
#define false 0
static int stop = false;
static void handle_term(int sig)
{
	stop = true;
}

int main(int argc, char* argv[])
{
	signal(SIGTERM, handle_term);
	if (argc <= 2)
	{
		printf("usage: %s ip_address port_number\n",
			basename(argv[0]));
		return 1;
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);
	int backlog = 5;

	int sock = socket(PF_INET, SOCK_STREAM, 0);
	assert( sock >= 0);

	/* 创建一个IPv4 socket地址*/
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);

	int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
	assert( ret != -1);

	ret = listen(sock, backlog);
	assert(ret != -1);

	/*暂停20秒等待客户端连接和操作*/
	sleep(20);
	struct sockaddr_in client;
	socklen_t client_addrlength = sizeof(client);
	int connfd = accept(sock, (struct sockaddr*)&client, &client_addrlength);
	if (connfd < 0)
	{
		printf("errno is : %d\n", errno);
	}
	else
	{
		char remote[INET_ADDRSTRLEN];
		printf("connected with ip: %s and port: %d\n", inet_ntop(AF_INET,
			&client.sin_addr, remote, INET_ADDRSTRLEN), ntohs(client.sin_port));
		close(connfd);
	}

	close(sock);

	return 1;
}