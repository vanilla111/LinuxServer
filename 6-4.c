#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>

int main(int argc, char const *argv[])
{
	if (argc <= 2)
	{
		printf("usage: %s ip port\n", basename(argv[0]));
		return 1;
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);

	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);

	int sock = socket(PF_INET, SOCK_STREAM, 0);
	assert(sock >= 0);
	int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
	assert(ret != -1);
	ret = listen(sock, 5);
	assert(ret != -1);

	struct sockaddr_in client;
	socklen_t client_addrlength = sizeof(client);
	int connfd = accept(sock, (struct sockaddr*)&client, &client_addrlength);
	if (connfd < 0)
	{
		printf("connfd error :%d\n", errno);
	}
	else
	{
		int pipefd[2];
		ret = pipe(pipefd);
		assert(ret != -1);
		ret = splice(connfd, NULL, pipefd[1], NULL, 32768,
			SPLICE_F_MORE | SPLICE_F_MOVE);
		assert( ret != -1);
		ret = splice(pipefd[0], NULL, connfd, NULL, 32768,
			SPLICE_F_MORE | SPLICE_F_MOVE);
		assert(ret != -1);
		close(connfd);
	}
	close(sock);
	return 0;
}
