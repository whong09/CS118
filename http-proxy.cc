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
	int sockfd = 0, n = 0;
    char recvBuff[1024];
    char sendBuff[1024];
    struct sockaddr_in serv_addr;
    memset(recvBuff, '0',sizeof(recvBuff));
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(req.GetPort());
    inet_pton(AF_INET, req.GetHost().c_str(), &serv_addr.sin_addr);
    connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr));
    req.FormatRequest(sendBuff);
    write(sockfd, sendBuff, strlen(sendBuff));
    stringstream s;
    s.str("");
    while((n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
    {
        recvBuff[n] = 0;
        s << recvBuff;
    }
    return s.str();
}

int main(int argc, char *argv[])
{
	HttpRequest req;
	req.SetHost(argv[1]);
	req.SetPort(80);
	req.SetMethod(HttpRequest::GET);
	req.SetPath("/");
	req.SetVersion("1.1");
	//must close connection or hangs
	req.AddHeader("Connection", "close");
	string s = get_remote_page(req);
	cout << s;
    return 0;
}
