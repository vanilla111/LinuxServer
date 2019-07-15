#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define BUFFER_SIZE 4096

/* 主机状态 */
enum CHECK_STATE
{
	CHECK_STATE_REQUESTLINE = 0, // 正在分析请求行
	CHECK_STATE_HEADER           // 正在分析头部字段
};

/* 状态机的三种可能状态 */
enum LINE_STATUS
{
	LINE_OK = 0, // 完整行
	LINE_BAD,    // 行出错
	LINE_OPEN    // 尚不完整
};

/* HTTP请求结果 */
enum HTTP_CODE
{
	NO_REQUEST,        // 请求不完整，需要继续读取
	GET_REQUEST,       // 获得了一个完整的请求结果
	BAD_REQUEST,       // 请求语法错误
	FORBIDDEN_REQUEST, // 客户权限不够
	INTERNAL_ERROR,    // 服务器内部错误
	CLOSED_CONNECTION  // 客户端已经关闭链接
};

/* 简化设计，仅应答成功或者失败 */
static const char* szret[] = {
	"HTTP/1.1 200 OK\rContent-Type: text/html; charset=UTF-8\r\nI get a correct result\n",
	"HTTP/1.1 200 OK\rContent-Type: text/html; charset=UTF-8\r\nWhoops, Sometion wrong\n"
};
// static const char* szret[] = {"I get a correct result\n", "Hoops,Sometion wrong\n"};

LINE_STATUS parse_line(char* buffer, int &checked_index, int &read_index)
{
	char temp;
	/* checked_index指向目前buffer正在分析的字符
	*  read_index指向buffer中客户端数据尾部的下一个字节
	*/
	for (; checked_index < read_index; ++checked_index)
	{
		temp = buffer[ checked_index ];
		if (temp == '\r') // 可能读到一个完整的行
		{
			if ((checked_index + 1) == read_index) // \r是最后一个被读入的数据，那么需要继续读取才能进一步分析
			{
				return LINE_OPEN;
			}
			else if (buffer[checked_index + 1] == '\n') // 说明读到一个完整的行
			{
				buffer[checked_index++] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD; // 请求语法错误
		}
		else if (temp == '\n') // 也可能是完整的行
		{
			if ((checked_index > 1) && buffer[checked_index - 1] == '\r')
			{
				buffer[checked_index++] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD; // \r\n 应该成对出现
		}
	}
	/* 如果内容分析完毕也没有遇到\r字符，表示还需要读取才能进一步分析 */
	return LINE_OPEN;
}


/**
 * 解析 GET /search HTTP/1.1 的请求行
 * @param  temp       请求数据存放缓冲区
 * @param  checkstats 解析状态
 * @return            解析结果
 */
HTTP_CODE parse_requestline(char* temp, CHECK_STATE &checkstats)
{
	/* 找到temp第一个空白符或者\t所在的位置（地址） */
	char* url = strpbrk(temp, " \t"); // 请求行没有空白或'\t'，请求一定有问题
	if (!url)
	{
		return BAD_REQUEST;
	}
	*url++ = '\0';

	char* method = temp;
	/* 比较s1和s2大小，忽略大小写 */
	if (strcasecmp(method, "GET") == 0) // 暂时近支持GET方法
	{
		printf("The request method is GET\n");
	}
	else
	{
		return BAD_REQUEST;
	}

	/* strspn返回str1中第一个不在str2中的字符位置（偏移） */
	url += strspn(url, " \t");
	char* version = strpbrk(url, " \t");
	if (!version)
	{
		return BAD_REQUEST;
	}
	*version++ = '\0';
	version += strspn(version, " \t");
	/* 仅支持HTTP1.1 */
	if (strcasecmp(version, "HTTP/1.1") != 0)
	{
		return BAD_REQUEST;
	}
	/* 比较s1和s2前n个字符的大小，忽略大小写 */
	if (strncasecmp(url, "http://", 7) == 0)
	{
		url += 7;
		/* strchr寻找s1中第一次出现ch的位置（地址） */
		url = strchr(url, '/');
	}

	if (!url || url[0] != '/')
	{
		return BAD_REQUEST;
	}
	printf("The request URL is: %s\n", url);
	checkstats = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

/**
 * 分析头部字段比如下面之类的
 * Accept: 、Referer: 、Accept-Language: 、Accept-Encoding: 、User-Agent: 、 Host: 
 */
HTTP_CODE parse_headers(char* temp)
{
	/* 遇到空行，此处已经被request_line函数将\r\n替换为两个结束符，HTTP请求正确 */
	if (temp[0] == '\0')
	{
		return GET_REQUEST;
	}
	else if (strncasecmp(temp, "Host:", 5) == 0) // 只打印Host参数的值
	{
		temp += 5;
		temp += strspn(temp, " \t");
		printf("Teh request host is: %s\n", temp);
	}
	else  // 其他参数暂时忽略
	{
		printf("I can not handle this header\n");
	}
	return NO_REQUEST;
}

/* 分析HTTP请求入口函数 */
HTTP_CODE parse_content(char* buffer, int &checked_index, CHECK_STATE &checkstats, int &read_index, int &start_line)
{
	LINE_STATUS linestatus = LINE_OK; // 当前行读取状态
	HTTP_CODE retcode = NO_REQUEST;   // 记录HTTP请求的处理结果
	while ( (linestatus = parse_line(buffer, checked_index, read_index)) == LINE_OK )
	{
		char* temp = buffer + start_line;
		start_line = checked_index;
		switch (checkstats)
		{
			case CHECK_STATE_REQUESTLINE:
			{
				retcode = parse_requestline(temp, checkstats);
				if (retcode == BAD_REQUEST)
				{
					return BAD_REQUEST;
				}
				break;
			}
			case CHECK_STATE_HEADER:
			{
				retcode = parse_headers(temp);
				if (retcode == BAD_REQUEST)
				{
					return BAD_REQUEST;
				}
				else if (retcode == GET_REQUEST)
				{
					return GET_REQUEST;
				}
				break;
			}
			default:
			{
				return INTERNAL_ERROR;
			}
		}
	}
	if (linestatus == LINE_OPEN)
	{
		return NO_REQUEST;
	}
	else
	{
		return BAD_REQUEST;
	}
}

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

	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd != -1);
	int reuse = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	assert(ret != -1);
	ret = listen(listenfd, 5);
	assert(ret != -1);
	struct sockaddr_in client_address;
	socklen_t client_addrlength = sizeof(client_address);
	int fd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
	if (fd < 0)
	{
		printf("errno is : %d\n", errno);
	}

	char buffer[BUFFER_SIZE];
	memset(buffer, '\0', BUFFER_SIZE);
	int data_read = 0;
	int read_index = 0;
	int start_line = 0;
	int checked_index = 0;
	CHECK_STATE checkstats = CHECK_STATE_REQUESTLINE;
	while (1)
	{
		data_read = recv(fd, buffer + read_index, BUFFER_SIZE - read_index, 0);
		if (data_read == -1)
		{
			printf("reading failed\n");
			break;
		}
		else if (data_read == 0)
		{
			printf("remote client has closed the connection\n");
			break;
		}
		read_index += data_read;
		HTTP_CODE result = parse_content(buffer, checked_index, checkstats, read_index, start_line);

		if (result == NO_REQUEST)
		{
			continue;
		}
		else if (result == GET_REQUEST)
		{
			send(fd, szret[0], strlen(szret[0]), 0);
			break;
		}
		else
		{
			send(fd, szret[1], strlen(szret[1]), 0);
			break;
		}
	}
	close(fd);
	close(listenfd);
	return 0;
}