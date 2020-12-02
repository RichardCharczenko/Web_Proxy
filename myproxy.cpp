#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sstream>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "pthread.h"


#define BACKLOG 10 // how many pending connections queue will hold
#define BUFFERSIZE 256 // size of buffer for sending/recieving data
const std::string PORT = "80"; // default port used if no port specified by client request.
const std::string VERSION = "HTTP/1.0";
const std::string CLOSE = "Connection: close";
const std::string LINE_RETURN = "\r\n";

const int POOL_SIZE = 50;
int POOL = 0;
pthread_mutex_t lock;

struct thread_args{
  int socket;
};
// returns true on success, false on failure
bool create_socket(int &sockfd, struct addrinfo *servinfo);

// returns true on success, false on failure
bool recieve_message(int socket, std::string &message);
void send_message(int socket, std::string message);
// proxy recieves HTTP response and directly sends to client
void transfer_message(int proxy_sock, int client_sock);

// send 500 to client for any proxy error
void send_error(int socket);

bool parse_HTTP_request(std::string message, std::string &host, std::string &port, std::string &path, std::string &headers);
std::string parse_extra_headers(std::string headers, std::string toFind);
void send_HTTP_request(std::string url, std::string port, std::string path, std::string headers, int client_sock);

void* thread_driver(void* args){
  struct thread_args *t = (struct thread_args *) args;
  int new_fd = t->socket;
  delete t;

  std::cout << "Thread running" << std::endl;
  std::string socketMessage = "";
  std::string HTTP_host = "";
  std::string HTTP_port = "";
  std::string HTTP_path = "";
  std::string HTTP_headers = "";


  if (!recieve_message(new_fd, socketMessage))
    {
      send_error(new_fd);
    }


  if (!parse_HTTP_request(socketMessage, HTTP_host, HTTP_port, HTTP_path, HTTP_headers))
    send_error(new_fd);
  send_HTTP_request(HTTP_host, HTTP_port, HTTP_path, HTTP_headers, new_fd);

  pthread_mutex_lock(&lock);
  pthread_detach(pthread_self());
  close(new_fd);
  POOL--;
  pthread_mutex_unlock(&lock);
  return NULL;
}


int main(int argc, char *argv[]){
  pthread_mutex_init(&lock,NULL);
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints, *servinfo;
    int sockfd, new_fd, rv;

	if (argc != 2) {
	    std::cerr << "Usage: " << argv[0] << " PORT NO" << std::endl;
	    return 1;
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

	if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
		return 1;
	}

	create_socket(sockfd, servinfo);
    freeaddrinfo(servinfo);

    if(listen(sockfd, BACKLOG) == -1)
        return 2;

    while(1) {
      if(POOL < POOL_SIZE){
        addr_size = sizeof their_addr;

        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (new_fd == -1) {
            continue;
        }
        pthread_mutex_lock(&lock);
        POOL++;
        pthread_mutex_unlock(&lock);
        //create thread arguments
        struct thread_args *t_args = new struct thread_args;
        t_args->socket = new_fd;
        //start thread
        pthread_t tID;
        int state =  pthread_create(&tID, NULL, thread_driver, (void*)t_args);
        if (state != 0)
          exit(0);
        }
    }
    return 0;
}

bool create_socket(int &sockfd, struct addrinfo *servinfo){
	int yes=1;
	struct addrinfo *p;
	// loop through all the results and create socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client");
			continue;
		}
		if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes) == -1) {
			perror("setsockopt");
			exit(1);
		}
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
			perror("client");
            continue;
        }
		break;
	}

	if (p == NULL)
		return false;

	return true;
}

bool recieve_message(int socket, std::string &message) {
	char buff[BUFFERSIZE];
	message = "";
	std::size_t nbytes;
	std::string next_string = "";
	while(true) {
		nbytes = recv(socket, (void*) buff, BUFFERSIZE, 0);
		if(nbytes < 0)
			return false;
		if(nbytes == 0)
			return true;
		buff[nbytes] = '\0';
		next_string = std::string(buff);
		message += next_string;
		if(message.find("\r\n\r\n") != std::string::npos)
			return true;
	}
    return true;
}

void send_message(int socket, std::string message) {
	const char* buff = message.c_str();
	int len = message.length() + 2;
	int nbytes = 0;
	int sent_data = 0;
	while(nbytes < len){
		sent_data = send(socket, (void*) (buff + nbytes), len - nbytes, 0);
		if(sent_data == -1) {
			perror("Could not send HTTP request to server");
			break;
		}
		nbytes += sent_data;
	}
}

void transfer_message(int proxy_sock, int client_sock) {
	char buff[BUFFERSIZE];
	int nbytes = 0;
	while((nbytes = (recv(proxy_sock, (void*) buff, BUFFERSIZE, 0))) > 0) {
		// FIXME: surround in loop
		send(client_sock, buff, nbytes, 0);
	}
    return;
}

void send_error(int socket){
	std::string message = "500 Internal Error\r\n";
	send_message(socket, message);
  pthread_mutex_lock(&lock);
  POOL--;
  pthread_mutex_unlock(&lock);
  pthread_detach(pthread_self());
	close(socket);
  pthread_exit(0);
}

// Returns true if HTTP request is valid
bool parse_HTTP_request(std::string message, std::string &host, std::string &port, std::string &path, std::string &headers){
	std::stringstream ss(message);
	std::string word;
	int count = 0;
	std::size_t found_path, found_port, found_return;
	const std::string host_header = "Host:";
	const std::string connection_header = "Connection:";

	// check <METHOD>
	ss >> word;
	// From RFC 1945, Section 5.1.2: method names are case-sensitive
	// proxy only supports GET method
	if(word != "GET")
		return false;

	// validate and process <URL>
	ss >> host;
	if(host.find("http://") != 0)
		return false;
	host.erase(0, 7); // remove "http://"
	if((found_path = host.find("/")) == std::string::npos)
		return false;
	path = host.substr(found_path);
	if((found_port = host.find(":")) != std::string::npos){
		port.assign(host.begin()+found_port+1, host.begin()+found_path);
		host.erase(host.begin()+found_port, host.end());
	}
	else{
		port = PORT;
		host.erase(host.begin()+found_path, host.end());
	}

	// validate <HTTP VERSION>
	ss >> word;
	if (word != VERSION)
		return false;

	// get rest of client headers
	std::getline(ss, headers, '\0');
	// remove extra Host: header
	headers = parse_extra_headers(headers, host_header);
	// remove extra Connection: header
	headers = parse_extra_headers(headers, connection_header);
	// remove extra line returns
	while((found_return = headers.find(LINE_RETURN+LINE_RETURN)) != std::string::npos){
		headers.erase(found_return);
	}
	return true;
}

std::string parse_extra_headers(std::string headers, std::string toFind){
	size_t found, new_line;
	if ((found = headers.find(toFind)) != std::string::npos){
		new_line = headers.find("\n",found);
		headers.erase(headers.begin()+found, headers.begin()+new_line+1);
	}
	return headers;
}

void send_HTTP_request(std::string url, std::string port, std::string path, std::string headers, int client_sock){
	int sockfd, rv;
	struct addrinfo hints, *servinfo, *p;
    int yes=1;

    const std::string method = "GET ";
    const std::string version = " HTTP/1.0";
    const std::string connection = "Connection: close";

    std::string headerMessage = method + path + version + LINE_RETURN +
    "Host: " + url + ":" + port + LINE_RETURN + connection + headers + LINE_RETURN + LINE_RETURN;

    char* charUrl = const_cast<char*>(url.c_str());
	char* charport = const_cast<char*>(port.c_str());

    std::string serverMessage = "";
	std::cout << "headerMessage: " << headerMessage << std::endl;


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(charUrl, charport, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			send_error(client_sock);
			continue;
		}
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			send_error(client_sock);
			continue;
		}
		break;
	}

	if (p == NULL) {
		send_error(client_sock);
		return;
	}
	freeaddrinfo(servinfo);

    send_message(sockfd, headerMessage);

    transfer_message(sockfd, client_sock);

	close(sockfd);

	return;
}
