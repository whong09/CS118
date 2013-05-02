#include <sstream>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <map>
#include "http-request.h"
#include "http-response.h"
#include "compat.h"

#define LIM 10
#define STDIN 0

using namespace std;

int connectNum = 0;
int sendall(int s, char *buf, int *len)
{
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n==-1?-1:0; // return -1 on failure, 0 on success
}

int getRemoteSocket(HttpRequest req)
{
	int serverSock, status;
	struct addrinfo *servinfo, *p;
	struct addrinfo hints;
	memset(&hints,0,sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	if((status = getaddrinfo(req.GetHost().c_str(), "http", &hints, &servinfo)) != 0)
		perror(gai_strerror(status));
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((serverSock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			continue;
		}
		if (connect(serverSock, p->ai_addr, p->ai_addrlen) == -1) {
			close(serverSock);
			continue;
		}
		break;
	}
	if(p==NULL)
		perror("Connect Error");
	freeaddrinfo(servinfo);
	connectNum++;
	return serverSock;
}

void send_remote_req(HttpRequest req, int serverSock)
{
	char sendBuff[100000];
	memset(sendBuff, 0, 100000);
	req.FormatRequest(sendBuff);
	int length = req.HttpRequest::GetTotalLength();
	if(sendall(serverSock,sendBuff,&length) == -1)
	{
		perror("Sending request error");
	}
}

string get_remote_resp(int serverSock)
{
	int n;
	char recvBuff[100000];
	string s="";
	memset(recvBuff, 0, 100000);
	while((n = recv(serverSock, recvBuff, sizeof(recvBuff),0)) > 0)
	{
		s.append(recvBuff,n);
	}
	return s;
}

string get_host(HttpRequest * req)
{
	if (req->GetHost().length()==0)
	{
		req->SetHost(req->FindHeader("Host"));
	}
	return req->GetHost();
}

unsigned short get_port(HttpRequest * req)
{
	if (req->GetPort()==0)
	{
		req->SetPort(80);
	}
	return req->GetPort();
}

void sigchld_handler(int s)       //reap dead process
{
	while(waitpid(-1, NULL, WNOHANG)>0);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
	int listensock, clientsock, n, pid, status;
	int port = 14805;
	int yes = 1;
	struct sockaddr_in server_addr, client_addr;
	struct sigaction sa;
	socklen_t client_addr_size;

	bzero(&server_addr,sizeof(server_addr));
	if ((listensock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	    perror("Socket Creating error");
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port); 

	if (setsockopt(listensock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	{
		//perror("setsockopt"); 
		exit(1);
	}  
	if (bind(listensock, (struct sockaddr *) &server_addr,sizeof(server_addr)) < 0)
	{
		close(listensock);
		//perror("Binding error");
		exit(1);
	}

	if(listen(listensock,10) == -1)
	{
		//perror("listen error");
		exit(1);
	}

	sa.sa_handler = sigchld_handler;	//reap all dead process
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa, NULL) == -1)
	{
		//perror("sigaction error");
		exit(1);
	}

	cout << "proxy: waiting for connections..." << endl;

	int numActive = 0;
	//declare socket sets
	for (;; ++numActive) 
	{
		for(; numActive >= LIM; --numActive)
			wait(&status);
	    bzero(&client_addr,sizeof(client_addr));
	    clientsock = accept(listensock, (struct sockaddr *)&client_addr, &client_addr_size);
	    if (clientsock < 0)
	    	perror("Accepting connection error");
	    pid = fork();
	    if(pid < 0)
	    	perror("Forking error");
	    else if (pid == 0)
	    {
			cout << "new process #" << getpid() << endl;
			char buffer[256];
			fd_set read_fds;
			FD_ZERO(&read_fds);
			FD_SET(clientsock, &read_fds); //add client socket to read sockets
			int maxsock = clientsock;
			map<string,int> hostmap;
			while(1)
			{
				struct timeval tv;
				tv.tv_sec = 15;
				if(select(maxsock+1,&read_fds,NULL,NULL,&tv)==-1)
				{
					perror("select error");
					exit(4);
				}
				if(FD_ISSET(clientsock, &read_fds))
				{
					string rstring = "";
			    	while(1)
			    	{
			        	bzero(buffer,256);
			        	n = read(clientsock,buffer,255);
						if (n == 0)		//telnet closed
							break;
			        	if (n < 0)
			        		perror("Read request error");
			        	rstring.append(buffer,n);
			        	if (memmem(rstring.c_str(), rstring.length(), "\r\n\r\n", 4) != NULL)
			        		break; //check end with \r\n\r\n
					}
					if (n == 0) //client closed connection
					{
						cout << "process #" << getpid() << " is killed" << endl;
						close(clientsock);
						exit(1);
						break;
					}
			      	HttpRequest client_req;
					try	{
						client_req.ParseRequest(rstring.c_str(),rstring.length());
					} catch(ParseException& p) {
						string s = p.what();
						s += "\n";
						if (s == "Request is not GET\n")
							s="501 Not Implemented\n";
						else
							s="400 Bad Request\n";
						write(clientsock, s.c_str(), s.length());
						continue;
					}
					get_host(&client_req);
					get_port(&client_req);
					client_req.ModifyHeader("Connection","keep-alive");
					int remotesock;
					if(hostmap.find(client_req.GetHost()) == hostmap.end())
					{
						remotesock = getRemoteSocket(client_req);
						FD_SET(remotesock, &read_fds);	//add socket to read set
						hostmap[client_req.GetHost()] = remotesock;
						if(remotesock > maxsock)
							maxsock = remotesock;
					}
					else
						remotesock = hostmap[client_req.GetHost()];
					send_remote_req(client_req, remotesock);
				}
				else
				{
					int hostsock = -1;
					for(map<string,int>::iterator it=hostmap.begin(); it!=hostmap.end(); it++)
					{
						if(FD_ISSET(it->second, &read_fds))
						{
							hostsock = it->second;
							break;
						}
					}
					if(hostsock == -1)
					{
						perror("Host response dropped.");
						continue;
					}
					string s = get_remote_resp(hostsock);
					HttpResponse client_resp;
					client_resp.ParseResponse(s.c_str(),s.length());
					int old_len = client_resp.HttpResponse::GetTotalLength();
					client_resp.ModifyHeader("Connection","keep-alive");
					int len = client_resp.HttpResponse::GetTotalLength();
					char sendBuff[len+1];
					client_resp.FormatResponse(sendBuff);
					string sendstring = "";
					sendstring.append(sendBuff,len);
					sendstring.append(s.substr(old_len).c_str(),s.length()-old_len);
					if(write(clientsock,sendstring.c_str(),sendstring.length()) == -1)
						perror("Sending response error");
				}
			}
	    }
	}
	close(listensock);
	  return 0;
}
