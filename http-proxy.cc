#include <sstream>
#include <iostream>
#include <vector>
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
#include <sys/wait.h>

#define LIM 10

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

vector<string> get_remote_pages(vector<HttpRequest> req)
{
	int sockfd = 0, n = 0, status;
	char recvBuff[100000];
	char sendBuff[100000];
	struct addrinfo *servinfo, *p;
	struct addrinfo hints;
	vector<string> ret;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	memset(recvBuff, 0, 100000);
	memset(sendBuff, 0, 100000);
	if((status = getaddrinfo(req[0].GetHost().c_str(), "http", &hints, &servinfo)) != 0)
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

	for(size_t i = 0; i < req.size(); i++)
	{
		req[i].FormatRequest(sendBuff);
		int length = req[i].HttpRequest::GetTotalLength();
		if(sendall(sockfd,sendBuff,&length) == -1)
		{
			perror("Sending request error");
		}
		memset(sendBuff, 0, 100000);
	}
	for(size_t i = 0; i < req.size(); i++)
	{
		string s = "";
		while((n = recv(sockfd, recvBuff, sizeof(recvBuff)-1,0)) > 0)
		{
			s.append(recvBuff,n);
			cout << s << endl;
		}
		ret.push_back(s);
		memset(recvBuff, 0, 100000);
	}
	close(sockfd);
	return ret;
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
	/*int sockfd, newsockfd, n, pid, status;
	int port = 14805;
	int yes = 1;
	struct sockaddr_in server_addr, client_addr;
	struct sigaction sa;
	socklen_t client_addr_size;

	bzero(&server_addr,sizeof(server_addr));
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	    perror("Socket Creating error");
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port); 

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	{
		perror("setsockopt"); 
	}  
	if (bind(sockfd, (struct sockaddr *) &server_addr,sizeof(server_addr)) < 0)
		perror("Binding error");
	if(listen(sockfd,10) == -1)
	{
		perror("listen error");
	}

	sa.sa_handler = sigchld_handler;	//reap all dead process
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa, NULL) == -1)
	{
		perror("sigaction error");
	}

	cout << "proxy: waiting for connections..." << endl;

	int numActive = 0;
	for (;true; ++numActive)
	{
	    bzero(&client_addr,sizeof(client_addr));
	    newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_size);
	    if (newsockfd < 0)
	    	perror("Accepting connection error");
		for(; numActive >= LIM; --numActive)
			wait(&status);
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
					if (n == 0)
						break;
		        	if (n < 0)
		        		perror("Read request error");
		        	from_client_str.append(buffer,n);
		        	//check end with \r\n\r\n
		        	if (memmem(from_client_str.c_str(), from_client_str.length(), "\r\n\r\n", 4) != NULL)
		        		break;
				}
				if (n == 0) //client closed connection
				{
					cout << "process #" << getpid() << " is killed" << endl;
					close(newsockfd);
					exit(1);
				}
		      	HttpRequest client_req;
				try
				{
					client_req.ParseRequest(from_client_str.c_str(),from_client_str.length());
				}
				catch(ParseException& p)
				{
				
					string ss = p.what();
					//ss += "\n";
					if (ss == "Request is not GET\n")
						ss="Not Implemented 501\n";
					else
						ss="Bad Request 400\n";
					write(newsockfd, ss.c_str(), ss.length());
					continue;
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
	close(sockfd);
	  return 0;*/
	vector<HttpRequest> req;
	for(int i = 0; i < 2; i++)
	{
		HttpRequest r;
		r.SetHost("www.google.com");
		r.SetPort(80);
		r.SetMethod(HttpRequest::GET);
		r.SetVersion ("1.1");
        r.AddHeader ("Accept-Language", "en-US");
        req.push_back(r);
	}
	req[0].SetPath("/images/srpr/logo4w.png");
	req[1].SetPath("/images/nav_logo123.png");
	vector<string> s = get_remote_pages(req);
	cout << s[0] << endl;
	cout << s[1] << endl;
}
