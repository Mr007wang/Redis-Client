#include "util.h"
#include <stdlib.h>
#include <string.h>
using namespace utility;
/*
 * 从这个文件开始,我就要开始写redis的客户端了.
 */

const int BUFF_SIZE = 1024;
const int MAX_ARGC = 15;
const int MAX_ARGV_LEN = 1024;
const char crlf[] = "\r\n";

static char* left_trim(char* buf) { /* 去掉左边的空格 */
	while (*buf == ' ') buf++;
	return buf;
}

static void handle_escape_sequence(char* buf) { /* 处理转义字符 */
	static char temp[MAX_ARGV_LEN];
	bool left_quote = false;	/* 是否存在左引号 */
	int count = 0;
	int i = 0;
	if (buf[0] == '"') {
		left_quote = true;
		count++;
	}
	for ( ; buf[count] != 0; ++i) { /* 转义的功能暂时还未实现 */
		temp[i] = buf[count];
		count++;
	}
	if (left_quote)
		temp[i - 1] = 0;
	else
		temp[i] = 0;
	strcpy(buf, temp); /* 将数据拷贝回去 */
}

void translate(char* buf) { /* 将输入的东西转化为要发出的命令 */
	/* 首先将字符按照空格解析成一个一个的 element */
	char* end = NULL;
	char* start = left_trim(buf);
	int argc = 0;
	int n = 0;
	static char argv[MAX_ARGC][MAX_ARGV_LEN];
	while ((end = strchr(start, ' ')) != NULL) { /* 寻找空格 */
	/* 然后拷贝 */
		if (*start == '"') { /* 如果是一个"开头,那么要拷贝到"为止 */
			int count = 0;
			start++;
			while (*start != '"') {
				argv[argc][count] = *start;
				start++; count++;
			}
			argv[argc][count] = 0;
			start++; /* 去除" */ 
			start = left_trim(start);
		}
		else {
			n = end - start;
			strncpy(argv[argc], start, n);
			argv[argc][n] = 0;
			start = left_trim(end);
		}
		argc++;
	}
	if (*start != '\n') {
		n = strlen(start) - 1;
		if (*start == '"') { /* 处理掉双引号 */
			strncpy(argv[argc], start + 1, n - 1); /* 去掉末尾的空格 */
			n -= 2;
		}
		else {
			strncpy(argv[argc], start, n); /* 去掉末尾的空格 */
		}
		argv[argc][n] = 0;
	}
	else {
		argc--;
	}
	
	/* 然后重新组装命令 */
	start = buf;
	/* 首先是命令的个数 */
	n = sprintf(start, "*%d\r\n", argc + 1);
	start += n;
	/* 然后一个命令一个命令地处理 */
	int len = 0;
	for (int i = 0; i <= argc; ++i) {
		len = strlen(argv[i]); /* 获取长度 */
		n = sprintf(start, "$%d\r\n", len); /* 写入长度信息 */
		start += n;
		
		strncpy(start, argv[i], len);
		start += len;
		
		strncpy(start, crlf, 2);
		start += 2;
	}
	*start = 0;
}

static int get_line(char *src, char *target) { /* 从src中读取一行数据 */
	char *start = src;
	char *end;
	end = strchr(start, '\n');
	int n = end - start - 1; /* 去除\r */
	strncpy(target, start, n);
	target[n] = 0;
	return n + 2; /* 一共读取的字节数目 */
}

void handle_reply(int fd) {
	static char read_buf[BUFF_SIZE];
	int argc; /* 命令的个数 */
	static char res[MAX_ARGV_LEN]; /* 用来存储接收到的结果 */
	int n = Read(fd, read_buf, sizeof(read_buf));
	read_buf[n] = 0;
	char *start = read_buf;
	/* 开始处理接收到的回复,下面是解析的过程 */
	
	/* 一般而言,第一个字符代表了接收的东西 */
	switch (start[0])
	{
	case '-': { /* 到这里表示出错了. */
		/* 格式一般为-ERR XXXXX\r\n */
		start += 5;
		printf("(error) %s", start);
		break;
	}
	case ':': { /* : 代表整数 */
		start += 1;
		start += get_line(start, res);
		printf("(integer) %s\n", res);
		break;
	}
	case '+':
		start += 1;
		start += get_line(start, res);
		printf("%s\n", res);
		break;
	case '$': {
		start += 1;
		start += get_line(start, res);
		n = atoi(res); /* 读取参数的大小 */
		if (n == -1) {
			printf("(nil)\n");
		}
		else {
			memcpy(res, start, n);
			res[n] = 0;
			start += n + 2;
			/* 这个字符必须要逐个输出 */
			printf("\"");
			for (int i = 0; i < n; ++i) {
				if (res[i] == 0)
					printf("\\x00");
				else
					printf("%c", res[i]);
			}
			printf("\"\n");
		}
		break;
	}
	case '*': { /* *代表有多个参数 */
		start += 1;
		start += get_line(start, res);
		argc = atoi(res); /* 参数的个数 */
		if (argc == 0) 
			printf("(empty list or set)\n");
		else{
			for (int i = 0; i < argc; ++i) {
				start += 1;
				start += get_line(start, res);
				n = atoi(res);
				if (n == -1) {
					printf("%d) (nil)\n", i + 1);
				}
				else {
					strncpy(res, start, n);
					res[n] = 0;
					start += n + 2;
					printf("%d) \"%s\"\n", i + 1, res);
				}
			}
		}
	}
		
	}
}

int main(int argc, char *argv[])
{
	int fd = Open_clientfd("127.0.0.1", 6379); /* 试图连接服务器 */
	char buf[BUFF_SIZE] = { 0 };
	/* 首先要向对方发送认证消息 */
	for (; ; ) {
		printf("> ");
		fflush(stdout); /* 刷新缓冲区 */
		int n = Read(fileno(stdin), buf, sizeof(buf));
		buf[n] = '\0';
		translate(buf);
		Write(fd, buf, strlen(buf));
		handle_reply(fd);
	}
}