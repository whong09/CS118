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
#include "http-request.h"
#include "http-response.h"
#include <signal.h>
#include "compat.h"

using namespace std;

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

string get_remote_page(HttpRequest req)
{
	int sockfd = 0, n = 0, status;
	char recvBuff[100000];
	char sendBuff[100000];
	struct addrinfo *servinfo, *p;
	struct addrinfo hints;
	string s="";
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	memset(recvBuff, 0, 100000);
	memset(sendBuff, 0, 100000);
	if((status = getaddrinfo(req.GetHost().c_str(), "http", &hints, &servinfo)) != 0)
		perror(gai_strerror(status));
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
	    	continue;
		}
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			continue;
		}
		break;
	}
	if(p==NULL)
		perror("Connect Error");
	freeaddrinfo(servinfo);
	req.FormatRequest(sendBuff);
	int length = req.HttpRequest::GetTotalLength();
	if(sendall(sockfd,sendBuff,&length) == -1)
	{
		perror("Sending request error");
	}
	while((n = recv(sockfd, recvBuff, sizeof(recvBuff)-1,0)) > 0)
	{
		s.append(recvBuff,n);
	}
	close(sockfd);
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


//////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
	int sockfd, newsockfd, n, pid;
	int port = 14805;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_size;
	bzero(&server_addr,sizeof(server_addr));
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	    perror("Socket Creating error");
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port); 
	int yes = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	{
		perror("setsockopt"); exit(1);
	}  
	if (bind(sockfd, (struct sockaddr *) &server_addr,sizeof(server_addr)) < 0)
		perror("Binding error");
	listen(sockfd,10);
	while (1) 
	{
	    bzero(&client_addr,sizeof(client_addr));
	    newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_size);
	    if (newsockfd < 0)
	    	perror("Accepting connection error");
	    pid = fork();
	    if(pid < 0)
	    	perror("Forking error");
	    else if (pid == 0)
	    {
			cout << "new process #" << getpid() << endl;
			char buffer[256];
			string from_client_str;
			while(1)
			{
				from_client_str = "";
		    	//getting the request string
		    	while(1)
		    	{
		        	bzero(buffer,256);
		        	n = read(newsockfd,buffer,255);
		        	if (n < 0)
		        		perror("Read request error");
		        	from_client_str.append(buffer,n);
		        	//check end with \r\n\r\n
		        	if (memmem(from_client_str.c_str(), from_client_str.length(), "\r\n\r\n", 4) != NULL)
		        		break;
				}
		      	HttpRequest client_req;
				try
				{
					client_req.ParseRequest(from_client_str.c_str(),from_client_str.length());
				}
				catch(ParseException& p)
				{
					string s = p.what();
					if (s == "Request is not GET")
						s="Not Implemented";
					write(newsockfd, s.c_str(), s.length());
					break;
				}
				get_host(&client_req);
				get_port(&client_req);
				client_req.ModifyHeader("Connection","close");
			  	string s = get_remote_page(client_req);
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
				if(write(newsockfd,sendstring.c_str(),sendstring.length()) == -1)
					perror("Sending response error");
			}
	    }
	}
	  return 0;
}

