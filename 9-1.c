#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char const *argv[])
{
	char *test_ip = "172.20.157.22";
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

	struct sockaddr_in client_address;
	socklen_t client_addrlength = sizeof(client_address);
	int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
	if (connfd < 0)
	{
		printf("errno : %d\n", errno);
		close(listenfd);
	}

	char buf[1024];
	fd_set read_fds;
	fd_set exception_fds;
	FD_ZERO(&read_fds);
	FD_ZERO(&exception_fds);

	while (1)
	{
		memset(buf, '\0', sizeof(buf));
		/* 每次调用select前都要重新fd_set中设置文件描述符 */
		/* 因为事件发生之后，文件描述符集合将被内核修改 */
		FD_SET(connfd, &read_fds);
		FD_SET(connfd, &exception_fds);
		ret = select(connfd + 1, &read_fds, NULL, &exception_fds, NULL);
		if (ret < 0)
		{
			printf("selection failure\n");
			break;
		}
		if (FD_ISSET(connfd, &read_fds))
		{
			ret = recv(connfd, buf, sizeof(buf) - 1, 0);
			if (ret <= 0) break;
			printf("get %d bytes of normal data: %s\n", ret, buf);
		}
		else if (FD_ISSET(connfd, &exception_fds))
		{
			ret = recv(connfd, buf, sizeof(buf) - 1, MSG_OOB);
			if (ret <= 0) break;
			printf("get %d bytes of oob data: %s\n", ret, buf);
		}
	}

	close(connfd);
	close(listenfd);
	return 0;
}