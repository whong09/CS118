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
#include "fcntl.h"
#include "sys/time.h"
#include "fstream"
#include "boost/functional/hash.hpp"
#include "boost/lexical_cast.hpp"

#define LIM 10
#define STDIN 0

using namespace std;


int hashCode(string s)
{
    boost::hash<std::string> string_hash;

    return string_hash(s);
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

string recv_timeout(int s , double timeout)
{
    int size_recv , total_size= 0;
    struct timeval begin , now;
	char chunk[100000];
    double timediff;
     
    //make socket non blocking
    fcntl(s, F_SETFL, O_NONBLOCK);
     
    //beginning time
    gettimeofday(&begin , NULL);
	string returnString = "";
    while(1)
    {
        gettimeofday(&now , NULL);
         
        //time elapsed in seconds
        timediff = (now.tv_sec - begin.tv_sec) + 1e-6 * (now.tv_usec - begin.tv_usec);
        //if you got some data, then break after timeout
        if( total_size > 0 && timediff > timeout )
        {
            break;
        }
         
        //if you got no data at all, wait a little longer, twice the timeout
        else if( timediff > timeout*2)
        {
            break;
        }
         
        memset(chunk ,0 , 100000);  //clear the variable
        if((size_recv =  recv(s , chunk , 99999 , 0) ) <= 0)
        {
            //if nothing was received then we want to wait a little before trying again, 0.1 seconds
            usleep(100000);
        }
        else
        {
            total_size += size_recv;
			returnString.append(chunk,size_recv);
            //reset beginning time
            gettimeofday(&begin , NULL);
        }
    }

	int flags = fcntl(s, F_GETFL);
	fcntl(s, F_SETFL, flags & ~O_NONBLOCK);
     
    return returnString;
}


int getRemoteSocket(HttpRequest req)
{
	int serverSock, status;
	if(get_host(&req) != "127.0.0.1")
	{
		struct addrinfo *servinfo, *p;
		struct addrinfo hints;
		memset(&hints,0,sizeof(hints));
		hints.ai_family = PF_INET;
		hints.ai_flags = AI_PASSIVE;
		hints.ai_socktype = SOCK_STREAM;

		if((status = getaddrinfo(get_host(&req).c_str(), "http", &hints, &servinfo)) != 0)
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
	}
	else
	{
		struct sockaddr_in serv_addr;
		serverSock = socket(AF_INET, SOCK_STREAM, 0);
		memset(&serv_addr, '0', sizeof(serv_addr)); 
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(get_port(&req));
		inet_pton(AF_INET, get_host(&req).c_str(), &serv_addr.sin_addr);
		connect(serverSock,(struct sockaddr *)&serv_addr,sizeof(serv_addr));
	}
	return serverSock;
}

void send_remote_req(HttpRequest req, int serverSock)
{
	int length = req.HttpRequest::GetTotalLength();
	char sendBuff[length+1];
	memset(sendBuff, 0, length);
	req.FormatRequest(sendBuff);
	if(write(serverSock,sendBuff,length) < 0)
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

void sigchld_handler(int s)       //reap dead process
{
	while(waitpid(-1, NULL, WNOHANG)>0);
}

HttpRequest generate_condition_req(HttpRequest req,int remotesock, map<int,string> reqrespmap)
{
	int len;
	fstream mycache;
	string filename;
	filename += (string)get_host(&req);
	filename += (string)req.GetPath();
	cout << "filename " << filename << endl;
	filename = boost::lexical_cast<std::string>(hashCode(filename));
	filename = "./cache/"+filename;
	cout << "filename " << filename << endl;
	mycache.open(filename.c_str(), ios::in);
	if(mycache)
	{
		mycache.seekp(0, ios_base::end);
		if((len=mycache.tellp()) != 0)
		{
			char buf[len];
			string s="";
			mycache.seekp(0,ios_base::beg);
			mycache.read(buf,len);
			s.append(buf,len);
			
			reqrespmap[remotesock] = s;
			cout << "the cache is\n" << s << endl;
			HttpResponse resp;
			resp.ParseResponse(buf,len);
			string date = resp.FindHeader("Date");
			req.ModifyHeader("If-Modified-Since",date);
		}
	}
	mycache.close();
	return req;
}

int write_to_cache(HttpRequest req, char * buf, int len)
{
	fstream mycache;
	string filename;
	filename += (string)get_host(&req);
	filename += (string)req.GetPath();
	cout << "filename " << filename << endl;
	filename = boost::lexical_cast<std::string>(hashCode(filename));
	filename = "./cache/"+filename;
	cout << "filename " << filename << endl;
	mycache.open(filename.c_str(), ios::out);
	if(!mycache)
		return -1;
	mycache.write(buf,len);
	mycache.close();
	return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
	int listensock, clientsock, n, pid, status;
	int port = 48809;
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
		perror("setsockopt"); 
		exit(1);
	}  
	if (bind(listensock, (struct sockaddr *) &server_addr,sizeof(server_addr)) < 0)
	{
		close(listensock);
		perror("Binding error");
		exit(1);
	}

	if(listen(listensock,10) == -1)
	{
		perror("listen error");
		exit(1);
	}

	sa.sa_handler = sigchld_handler;	//reap all dead process
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa, NULL) == -1)
	{
		perror("sigaction error");
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
			char buffer[1];
			fd_set read_fds;
			FD_ZERO(&read_fds);
			int maxsock = clientsock;
			map<string,int> hostmap;
			map<int,HttpRequest> reqmap;	//latest request associate with the sockfd
			map<int,string> reqrespmap;
			string hostname;
			int numSockReady = 0;
			while(1)
			{
			//	struct timeval tv;
			//	tv.tv_sec = 1;
				FD_SET(clientsock, &read_fds);
				if((numSockReady = select(maxsock+1,&read_fds,NULL,NULL,NULL))==-1)
				{
					perror("Select error");
				}
				if(numSockReady == 0)
				{
					sleep(1);
					continue;
				}
				for(int i=0; i<=maxsock && numSockReady>0; i++)
				{
					cout << "checking socket " << i << endl;
					if(FD_ISSET(i, &read_fds))
					{
						numSockReady -= 1;
						cout << "is in?" << endl;
						if(i==clientsock)
						{
							cout << "reading client" << endl;
							string rstring = "";
							//since the buffer size is 1, so we will read only 1 request at one time
					    	while(1)
					    	{
					        	bzero(buffer,1);
					        	n = read(clientsock,buffer,1);
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
							int remotesock;
							hostname = get_host(&client_req);
							cout << hostname << endl;
							if(hostmap.find(hostname) == hostmap.end())
							{
								/*int status;
								struct addrinfo *servinfo, *p;
								struct addrinfo hints;
								memset(&hints,0,sizeof(hints));
								hints.ai_family = PF_INET;
								hints.ai_flags = AI_PASSIVE;
								hints.ai_socktype = SOCK_STREAM;

								if((status = getaddrinfo(get_host(&client_req).c_str(), "http", &hints, &servinfo)) != 0)
									perror(gai_strerror(status));
								for(p = servinfo; p != NULL; p = p->ai_next) {
									if ((remotesock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
										continue;
									}
									if (connect(remotesock, p->ai_addr, p->ai_addrlen) == -1) {
										close(remotesock);
										continue;
									}
									break;
								}
								if(p==NULL)
									perror("Connect Error");
								freeaddrinfo(servinfo);*/
								
								remotesock = getRemoteSocket(client_req);
								FD_SET(remotesock, &read_fds);	//add socket to read set
								hostmap.insert(pair<string,int>(hostname,remotesock));
								if(remotesock > maxsock)
									maxsock = remotesock;
								cout << hostmap.find(hostname)->second << "in the map" << endl;
							}
							else
								{remotesock = hostmap.find(hostname)->second;FD_SET(remotesock, &read_fds);cout << remotesock << endl;}
							
							//generate the buffer to send to the remote server, either the original request
							// or the modified request with If-Modified-Since
							client_req = generate_condition_req(client_req,remotesock,reqrespmap);
							
							int cli_length = client_req.HttpRequest::GetTotalLength();
							char sendBuff[cli_length+1];
							memset(sendBuff, 0, cli_length);
							client_req.FormatRequest(sendBuff);
							cout << (string)sendBuff << endl;
							if(write(remotesock,sendBuff,cli_length) < 0)
							{
								perror("Sending request error");
							}
							reqmap[remotesock] = client_req;
							cout << "write complete" << endl;
							
							//send_remote_req(client_req, remotesock);
						}
						else
						{
							cout << "reading the remote" << endl;
							string s = recv_timeout(i,0.2);
							if(s=="")
							{
								cout << "resending" <<endl;
								close(i);
								HttpRequest redo;
								redo = reqmap.find(i)->second;
								int redosock = getRemoteSocket(redo);
								hostmap[get_host(&redo)] = redosock;
								send_remote_req(redo, redosock);
								reqmap.erase(reqmap.find(i));
								reqmap[redosock] = redo;
								hostmap.insert(pair<string,int>(get_host(&redo),redosock));
								if(redosock > maxsock)
									maxsock = redosock;
								FD_CLR(i, &read_fds);
								FD_SET(redosock, &read_fds);
								break;
							}
							else
							{
								FD_CLR(i, &read_fds);
								HttpResponse client_resp;
								try
								{
									client_resp.ParseResponse(s.c_str(),s.length());
								}
								catch(ParseException& p)
								{
									string c = p.what();
									c += "\n";
									//		if (c == "HTTP response doesn't end with \\r\\n\n")
									//			c="closed\n";
									//		write(clientsock, c.c_str(), c.length());
									cout << c << endl;
									//		hostmap.erase(i);
									continue;
								}

								string sendback = "";
								if(client_resp.GetStatusCode() == "304")
								{
									HttpRequest theReq = reqmap.find(i)->second;
									sendback = reqrespmap.find(i)->second;
								}
								else	//means modified, write it to file
								{
									char sendBuff[s.length()];
									strcpy(sendBuff,s.c_str());
									sendback = s;
									HttpRequest theReq = reqmap.find(i)->second;
									if(write_to_cache(theReq,sendBuff,s.length()) == -1)
										perror("Write to file failed");
								}
								
								if(write(clientsock,sendback.c_str(),sendback.length()) == -1)
									{cout << "here?" << endl;perror("Sending response error");}
							}
						}
					}
				}
			}
	    }
	}
	close(listensock);
	  return 0;
}
