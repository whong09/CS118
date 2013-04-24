/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

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

using namespace std;

string get_remote_page(HttpRequest req)
{
  int sockfd = 0, n = 0, status;
  char recvBuff[1024];
  char sendBuff[1024];
  struct addrinfo *servinfo, *p;
  memset(recvBuff, '0',sizeof(recvBuff));
  memset(sendBuff, '0', sizeof(sendBuff));
  if((status = getaddrinfo(req.GetHost().c_str(), "http", NULL, &servinfo)) != 0)
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
  write(sockfd, sendBuff, strlen(sendBuff));
  stringstream s;
  s.str("");
  while((n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
  {
    recvBuff[n] = 0;
    s << recvBuff;
  }
  close(sockfd);
  return s.str();
}

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
      string client_string="";
      
      //getting the request string
      while(1)
      {
        bzero(buffer,256);
        
        n = read(newsockfd,buffer,255);
        
        if (n < 0)
          perror("Read request error");
        
        client_string.append(buffer,n);
        
        //check end with \r\n\r\n
        //Todo: should use memmem to deal with persistent
        if (n==2 && buffer[0]=='\r' && buffer[1]=='\n')
          break;
      }
      
      HttpRequest client_req;
      
      client_req.ParseRequest(client_string.c_str(),client_string.length());
      
      cout << "The hostname that the client wants is: " << client_req.GetHost() << ":" << client_req.GetPort() << endl;
      
      
      

    }
  }
  
  /*HttpRequest req;
  req.SetHost(argv[1]);
  req.SetPort(80);
  req.SetMethod(HttpRequest::GET);
  req.SetPath("/");
  req.SetVersion("1.1");
  //must close connection or hangs
  req.AddHeader("Connection", "close");
  string s = get_remote_page(req);
  cout << s;
  return 0;*/
}
