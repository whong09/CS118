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

using namespace std;

string get_remote_page(HttpRequest req)
{
  int sockfd = 0, n = 0, status;
  char recvBuff[1024];
  char sendBuff[1024];
  struct addrinfo *servinfo, *p;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_INET;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_socktype = SOCK_STREAM;
  memset(recvBuff, 0,sizeof(recvBuff));
  memset(sendBuff, 0, sizeof(sendBuff));
  if((status = getaddrinfo(req.GetHost().c_str(), "http", &hints, &servinfo)) != 0)
    perror(gai_strerror(status));
  for(p = servinfo; p != NULL; p = p->ai_next) {
	cout << "checking ip" << endl;
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      continue;
    }
    break;
  }
cout << "socket created" << endl;
  if(p==NULL)
	perror("Connect Error");
  freeaddrinfo(servinfo);
  req.FormatRequest(sendBuff);
  write(sockfd, sendBuff, req.HttpRequest::GetTotalLength());
  string s="";
  while((n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
  {
	cout << "am i here?" << endl;
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
	char buffer[256];
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
		        	//Todo: should use memmem to deal with persistent
		        	if (n==2)
		        		break;
				}
		      	HttpRequest client_req;			
		      	client_req.ParseRequest(from_client_str.c_str(),from_client_str.length());
		      	cout << "The hostname that the client wants is: " << get_host(&client_req) <<  endl;
				cout << "The port number is: " << get_port(&client_req) << endl;
		      	client_req.AddHeader("Connection", "close");
			  	string s = get_remote_page(client_req);
			  	cout << s << endl;
				HttpResponse client_resp;
				client_resp.ParseResponse(s.c_str(),s.length());

				write(newsockfd,s.c_str(),s.length());
				cout << client_resp.FindHeader("Date") << endl;
			}
	    }
	}

	  //HttpRequest req;
	  //req=client_to_proxy();
	/*req.SetHost("www.google.com");
	  req.SetPort(80);
	  req.SetMethod(HttpRequest::GET);
	  req.SetPath("/");
	  req.SetVersion("1.1");
	  //must close connection or hangs
	*/
	  return 0;
}

