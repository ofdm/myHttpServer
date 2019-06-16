#include "http_conn.h"

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

const char *doc_root = "/yourpath";

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
	if(real_close&&(m_sockfd!=-1))
	{
		epoll_removefd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		m_user_count--;
	}
}

void http_conn::init(int sockfd, const sockaddr_in &addr)
{
	m_sockfd = sockfd;
	m_address = addr;
	//int reuse = 1;
	//setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	epoll_addfd(m_epollfd, sockfd, true);
	m_user_count++;

	init();
}

void http_conn::init()
{
	m_check_state = CHECK_STATE_REQUESTLINE;  //正在分析请求行
	m_linger = false;                         //若有数据待发送，我们不延迟关闭，哼！
	m_method = GET;                           //一开始肯定是GET
	m_url = 0;                                //客户请求的文件名
	m_version = 0;                            //HTTP协议版本号，这里只有HTTP/1.1
	m_content_length = 0;                     //请求的消息体长度
	m_host = 0;                               //主机名
	m_start_line = 0;                         //当前正在解析行的其实位置
	m_checked_idx = 0;                        //当前正在分析的字符在读缓冲区中的位置
	m_read_idx = 0;                           //标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
	m_write_idx = 0;                          //写缓冲区中待发送的字节数
	memset(m_read_buf, '\0', READ_BUFFER_SIZE);   //读缓冲
	memset(m_write_buf, '\0', WRITE_BUFFER_SIZE); //写缓冲
	memset(m_real_file, '\0', FILENAME_LEN);      //客户请求目标文件的完整路径（doc_root+m_url）
}

http_conn::LINE_STATUS http_conn::parse_line()   //通过m_checked_idx获取每一行,每一行后面都会有\r\n
{
	char temp;
	for(;m_checked_idx<m_read_idx;++m_checked_idx)
	{
		temp = m_read_buf[m_checked_idx];
		if(temp=='\r')
		{
			if((m_checked_idx+1)==m_read_idx)
				return LINE_OPEN;
			else if(m_read_buf[m_checked_idx+1]=='\n')
			{
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		else if(temp=='\n')
		{
			if((m_checked_idx>1)&&(m_read_buf[m_checked_idx-1]=='\r'))
			{
				m_read_buf[m_checked_idx-1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	return LINE_OPEN;
}

bool http_conn::read()
{
	if(m_read_idx>=READ_BUFFER_SIZE) //我初始化m_read_idx=0，READ_BUFFER_SIZE=2048，不会满足的
		return false;
	int bytes_read = 0; //待会要接收到的字节数
	while(true)
	{
		//recv这函数可能会小于我们期望读到的长度所以要多次读直到返回-1
		bytes_read = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
		if(bytes_read==-1)
		{
			//如果这两种错误说明读完了，这两种什么错误之后再查
			if(errno==EAGAIN||errno==EWOULDBLOCK) 
				break;
			return false;
		}
		//recv等于0说明对方已经关闭连接了
		else if(bytes_read==0)
			return false;
		m_read_idx += bytes_read;  //现在读缓冲区中已经读入的客户数据的最后一个字节的下一个位置要变了
	}
	return true;
}

//解析第一行GET
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) 
{
	m_url = strpbrk(text, " \t");
	if(!m_url)
		return BAD_REQUEST;
	*m_url++ = '\0';

	char *method = text;
	if(strcasecmp(method, "GET")==0)
		m_method = GET;
	else if(strcasecmp(method, "POST")==0)
		m_method = POST;
	else
		return BAD_REQUEST;

	m_url += strspn(m_url, " \t");
	m_version = strpbrk(m_url, " \t");
	if(!m_version)
		return BAD_REQUEST;
	*m_version++ = '\0';
	m_version += strspn(m_version, " \t");
	if(strcasecmp(m_version, "HTTP/1.1")!=0)
		return BAD_REQUEST;
	if(strncasecmp(m_url, "http://", 7)==0)
	{
		m_url += 7;
		m_url = strchr(m_url, '/');
	}
	if(!m_url||m_url[0]!='/')
		return BAD_REQUEST;

	m_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
	if(text[0]=='\0')
	{
		if(m_content_length!=0)
		{
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		return GET_REQUEST;
	}
	else if(strncasecmp(text, "Connection:", 11)==0)
	{
		text += 11;
		text += strspn(text, " \t");
		if(strcasecmp(text, "keep-alive")==0)
			m_linger = true;
	}
	else if(strncasecmp(text, "Content-Length:", 15)==0)
	{
		text += 15;
		text += strspn(text, " \t");
		m_content_length = atol(text);
	}
	else if(strncasecmp(text, "Host:", 5)==0)
	{
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	else 
		;//printf("oop! unkonw header %s\n", text);
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
	if(m_read_idx>=(m_content_length+m_checked_idx))
	{
		text[m_content_length] = '\0';
		return GET_REQUEST;
	}
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK;  //读到了一个完整的行
	HTTP_CODE ret = NO_REQUEST;         //处理HTTP请求的结果还不完整
	char *text = 0;
	while(((m_check_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status=parse_line())==LINE_OK))
	{
		text = get_line();
		m_start_line = m_checked_idx;
		//printf("got 1 http line: %s\n", text);

		switch(m_check_state)
		{
			case CHECK_STATE_REQUESTLINE:
			{
				ret = parse_request_line(text);  //解析http的请求行，就是第一行（GET。。。。）
				if(ret==BAD_REQUEST)
					return BAD_REQUEST;
				break;
			}
			case CHECK_STATE_HEADER:             //解析http头部信息
			{
				ret = parse_headers(text);
				if(ret==BAD_REQUEST)
					return BAD_REQUEST;
				else if(ret==GET_REQUEST)
					return do_request();
				break;
			}
			case CHECK_STATE_CONTENT:
			{
				ret = parse_content(text);
				if(ret==GET_REQUEST)
					return do_request();
				line_status = LINE_OPEN;
				break;
			}
			default:
				return INTERNAL_ERROR;
		}
	}
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);
	strncpy(m_real_file+len, m_url, FILENAME_LEN-len-1);

	//把m_real_file文件信息保存到m_file_stat结构体中，成功返回0
	if(stat(m_real_file, &m_file_stat)<0)   
		return NO_RESOURCE;
	//当前用户是否有读权限
	if(!(m_file_stat.st_mode&S_IROTH))
		return FORBIDDEN_REQUEST;
	//该文件是不是路径
	if(S_ISDIR(m_file_stat.st_mode))
		return BAD_REQUEST;

	int fd = open(m_real_file, O_RDONLY);

	//mmap：申请一段内存空间将open的fd直接映射到其中，它可以用于进程间通信的共享内存
	m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	return FILE_REQUEST;
}

void http_conn::unmap()
{
	if(m_file_address)
	{
		munmap(m_file_address, m_file_stat.st_size);
		m_file_address = 0;
	}
}

bool http_conn::write()
{
	int temp = 0;
	int bytes_have_send = 0;          //已经发送的字节数
	int bytes_to_send = m_write_idx;  //m_write_idx写缓冲区中待发送的字节数
	if(bytes_to_send==0) 
	{
		epoll_modfd(m_epollfd, m_sockfd, EPOLLIN);  //修改为读事件
		init();
		return true;
	}

	while(1)
	{
		temp = writev(m_sockfd, m_iv, m_iv_count);
		if(temp<=-1)
		{
			if(errno==EAGAIN)
			{
				epoll_modfd(m_epollfd, m_sockfd, EPOLLOUT);
				return true;
			}
			unmap();
			return false;
		}

		bytes_to_send -= temp;
		bytes_have_send += temp;
		if(bytes_to_send<=bytes_have_send)
		{
			unmap();
			if(m_linger)
			{
				init();
				epoll_modfd(m_epollfd, m_sockfd, EPOLLIN);
				return true;
			}
			else
			{
				epoll_modfd(m_epollfd, m_sockfd, EPOLLIN);
				return false;
			}
		}
	}
}

bool http_conn::add_response(const char *format, ...)
{
	if(m_write_idx>=WRITE_BUFFER_SIZE)
		return false;
	va_list arg_list;
	va_start(arg_list, format);
	int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list);
	if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx))
		return false;
	m_write_idx += len;
	va_end(arg_list);
	return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
	add_content_length(content_len);
	add_linger();
	add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
	return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
	return add_response("Connection: %s\r\n", (m_linger==true)?"keep-alive":"close");
}

bool http_conn::add_blank_line()
{
	return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
	return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
	switch(ret)
	{
		case INTERNAL_ERROR:
		{
			add_status_line(500, error_500_title);
			add_headers(strlen(error_500_form));
			if(!add_content(error_500_form))
				return false;
			break;
		}
		case BAD_REQUEST:
		{
			add_status_line(400, error_400_title);
			add_headers(strlen(error_400_form));
			if(!add_content(error_400_form))
				return false;
			break;
		}
		case NO_RESOURCE:
		{
			add_status_line(404, error_404_title);
			add_headers(strlen(error_404_form));
			if(!add_content(error_404_form))
				return false;
			break;
		}
		case FORBIDDEN_REQUEST:
		{
			add_status_line(403, error_403_title);
			add_headers(strlen(error_403_form));
			if(!add_content(error_403_form))
				return false;
			break;
		}
		case FILE_REQUEST:
		{
			add_status_line(200,ok_200_title);
			if(m_file_stat.st_size!=0)
			{
				add_headers(m_file_stat.st_size);
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				m_iv[1].iov_base = m_file_address;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				return true;
			}
			else
			{
				const char *ok_string = "<html><body></body></html>";
				add_headers(strlen(ok_string));
				if(!add_content(ok_string))
					return false;
			}
		}
		default:
			return false;
	}
	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	return true;

}

void http_conn::process()
{
	HTTP_CODE read_ret = process_read();
	if(read_ret==NO_REQUEST)
	{
		epoll_modfd(m_epollfd, m_sockfd, EPOLLIN);
		return;
	}

	bool write_ret = process_write(read_ret);
	if(!write_ret)
		close_conn();
	epoll_modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
