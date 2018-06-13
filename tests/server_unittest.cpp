#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// g++ server.cpp -g -o server
const std::string IP = "0.0.0.0";
const int PORT = 4312;
const int MAX_EVENT_COUNT = 5000; 
const int MAX_RECV_BUFF = 65535;

int listener_;
char buf_[MAX_RECV_BUFF];

int CreateListener();
bool Register(int kq, int fd);
void WaitEvent(int kq);
void HandleEvent(int kq, struct kevent* events, int nevents);
void Accept(int kq, int connSize);
void Receive(int sock, int availBytes);
void Enqueue(const char* buf, int bytes);

int main(int argc, char* argv[])
{
	listener_ = CreateListener();
	if (listener_ == -1)
		return -1;
	
    std::cout << "[INFO]IP : " << IP << ",PORT : " << PORT << std::endl;
	int kq = kqueue();
	if (!Register(kq, listener_))
	{
		std::cerr << "[ERROR]Register listener to kq failed.\n";
		return -1;
	}

	WaitEvent(kq);
	
	return 0;
}

int CreateListener()
{
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
	{
		std::cerr << "[ERROR]socket() failed:" << errno << std::endl;
		return -1;
	}
	
	struct sockaddr_in addr; 
	addr.sin_family = AF_INET; 
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = inet_addr(IP.c_str());
	if (bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr)) == -1)
	{
		std::cerr << "[ERROR]bind() failed:" << errno << std::endl; 
		return -1; 
	}

	if (listen(sock, 5) == -1)
	{
		std::cerr << "[ERROR]listen() failed:" << errno << std::endl;
		return -1;
	}

	return sock;
}

bool Register(int kq, int fd)
{
	struct kevent changes[1];
	EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD, 0, 0, NULL);

	int ret = kevent(kq, changes, 1, NULL, 0, NULL);
	if (ret == -1)
		return false;

	return true;
}

void WaitEvent(int kq)
{
	struct kevent events[MAX_EVENT_COUNT];
	while (true)
	{
		int ret = kevent(kq, NULL, 0, events, MAX_EVENT_COUNT, NULL);
		if (ret == -1)
		{
			std::cerr << "[ERROR]kevent failed!\n";
			continue;
		}

		HandleEvent(kq, events, ret);
	}
}

void HandleEvent(int kq, struct kevent* events, int nevents)
{
	for (int i = 0; i < nevents; i++)
	{
		int sock = events[i].ident;
		int data = events[i].data;

		if (sock == listener_)
			Accept(kq, data);
		else
			Receive(sock, data);
	}
}

void Accept(int kq, int connSize)
{
	for (int i = 0; i < connSize; i++)
	{
		int client = accept(listener_, NULL, NULL);
		if (client == -1)
		{
			std::cerr << "[ERROR]Accept failed.\n";
			continue;
		}

		if (!Register(kq, client))
		{
			std::cerr << "[ERROR]Register client failed.\n";
			return;
		}
	}
}

void Receive(int sock, int availBytes)
{
	int bytes = recv(sock, buf_, availBytes, 0);
	if (bytes == 0 || bytes == -1)
	{
		::close(sock);
		std::cerr << "[WARN]client close or recv failed.\n";
		return;
	}

	// Write buf to the receive queue.
	Enqueue(buf_, bytes);
    static const char send_buf[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
	int r = send(sock, send_buf, strlen(send_buf), 0);
    std::cout << "[INFO]send : " << send_buf << std::endl;
}

void Enqueue(const char* buf, int bytes)
{
	std::cout << "[INFO]recv : " << buf << std::endl;
}