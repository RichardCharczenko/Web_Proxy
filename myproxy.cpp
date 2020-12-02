#include <iostream>

#include <sstream>
#include <cstring>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>

using namespace std;

//Proxy settings
const int LENGTH = 2040;
const int HTMLLENGTH = 1000000;
const string PORT = "80"; // default port used if no port specified by client request.
const string VERSION = "HTTP/1.0";
const string CLOSE = "Connection: close";
const string HTTP = "http";
const int HTMLBUFFER = 10000;
const int RESPONSEBUFFER = 1000000;


void internal_error(int sock){
  char error[50] = "500 internal error \n";
  send(sock, (void *) &error, sizeof(error), 0);
};

void process_connection(int sock){
  int len = LENGTH;
  int htmlLength = HTMLLENGTH;

  //recieve request
  char buffer[LENGTH];
  memset(&buffer, '\0', LENGTH);
  char* buff_ptr = buffer;
  while(len){
    int bytes = recv(sock, buff_ptr, len, 0);
    if (bytes == -1){
      internal_error(sock);
      perror("recv");
    }
    len -= bytes;
    buff_ptr += bytes;
    if(buffer[strlen(buffer)-1] == '\n')
      break;
  } 
  //parse request
  string requestBuff = buffer;
  stringstream ss(requestBuff);
  string url, get, scheme, version, path, portNum;

  getline(ss, get, ' ');
  getline(ss, scheme, ':');
  ss.ignore(2);
  getline(ss, url, '/');
  getline(ss, path, ' ');
  getline(ss, version, ' ');
  path.insert(0, "/");

  if (scheme != HTTP){
    internal_error(sock);
    perror("incorrect scheme");
  }
  size_t found = version.find(VERSION);
  if(found != std::string::npos){
    version = VERSION;
  }
  else{
    internal_error(sock);
    perror("version");
  }

  found = url.find(':');
  if(found != std::string::npos){
    stringstream ssi(url);
    ssi.ignore(256, ':');
    getline(ssi, portNum, '/');
    ssi.clear();
    getline(ss, url, ':');
  }
  else{
    portNum = PORT;
  }
  if(get != "GET"){
    internal_error(sock);
    perror("invalid get request");
  }

  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  
  if((ret = getaddrinfo(url.c_str(), portNum.c_str(), &hints, &servinfo)) != 0){
    internal_error(sock);
    perror("Cannot get server info");
  }

  p = servinfo;
  while(p != NULL){
    if (p  == NULL){
      internal_error(sock);
      perror("Internal error connecting to server");
    }
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1 ){
      continue;
    }
    if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
      continue;
    }
    break;
  }

  if(sockfd <= 0){
    internal_error(sock);
    perror("Could not connect to client");
  }

  freeaddrinfo(servinfo);
  
  //send
  stringstream GETRequest;
  GETRequest << get << " " << path << " " << version << "\r\n" << "Host: "
             << url << "\r\n" << CLOSE << "\r\n\r\n";
  string request = GETRequest.str();
  char htmlRequest[HTMLBUFFER];
  memset(&htmlRequest, '\0', sizeof(htmlRequest));
  strcpy(htmlRequest, request.c_str());
  int bytes = send(sockfd, (void *) &htmlRequest, sizeof(htmlRequest), 0);
  if(bytes != (int) sizeof(htmlRequest)){
    internal_error(sock);
    perror("Could not send request");
  }
  
  //recieve
  char serverResponse[RESPONSEBUFFER];
  memset(&serverResponse, '\0', RESPONSEBUFFER);
  char *resp_buffer = serverResponse;
  while(htmlLength){
    int server_bytes = recv(sockfd, resp_buffer, htmlLength, 0);
    if (server_bytes <= 0){
      break;
    }
    htmlLength -= server_bytes;
    resp_buffer += server_bytes;
  }
  
  //forward
  int forward_bytes = send(sock, (void *)&serverResponse, sizeof(serverResponse), 0);
  if (forward_bytes != (int) sizeof(serverResponse)){
    internal_error(sock);
    perror("Could not forward server Response");
  }
  char newLine[2] = "\n";
  bytes = send(sock, (void*) &newLine, sizeof(newLine), 0);

  close(sock);
  return;
};


int main(int argc, char** argv){
  int server_fd, new_socket, valread;
  unsigned short port_num = atoi(argv[1]);
  int opt = 1;
  char buffer[1024] = {0};
    
  if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == 0){
    perror("socket failed");
    exit(EXIT_FAILURE);
  }
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons( port_num );
  
  //bind socket
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0){
    perror("bind failed");
    exit(EXIT_FAILURE);
  }
  
  if(listen(server_fd, 50) < 0){
    perror("listen");
    exit(EXIT_FAILURE);
  }
  
  while(true){
    struct sockaddr_in client;
    int clientlen = sizeof(client);

    if((new_socket = accept(server_fd, (struct sockaddr *)&client, (socklen_t*)&clientlen)) < 0){
      perror("accept");
      exit(EXIT_FAILURE);
    }
    
    process_connection(new_socket);
  }
  return 0;
}
