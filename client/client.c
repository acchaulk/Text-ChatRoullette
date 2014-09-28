/*
 * ** client.c -- a stream socket client demo
 * */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define PORT "3490" // the port client will be connecting to 

#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define INPUTSIZE 256 // max number for user input

// supported commands
#define CONNECT "connect"
#define CHAT "chat"
#define QUIT "quit"
#define TRANSFER "transfer"
#define FLAG "flag"
#define HELP "help"
#define QUIT "quit"

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int connect_to_server(char *hostname, char *port) {

    // TODO: use hostname instead of ip address

	printf("Connecting to %s : %s\n", hostname, port);
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	fd_set fdset;
	struct timeval tv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		fcntl(sockfd, F_SETFL, O_NONBLOCK);

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
		    if (errno != EINPROGRESS) { /* EINPORGRESS is expected */
				close(sockfd);
				perror("client: connect");
				continue;
			}
		}

		FD_ZERO(&fdset);
		FD_SET(sockfd, &fdset);
		tv.tv_sec = 10;             /* 10 second timeout */
		tv.tv_usec = 0;

		if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1) {
			int so_error;
			socklen_t len = sizeof so_error;

			getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);

			if (so_error == 0) {
				printf("%s:%s is open\n", hostname, port);
			}
		} 
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
		perror("recv");
		exit(1);
	}

	buf[numbytes] = '\0';

	printf("client: received '%s'\n",buf);

	close(sockfd);
	return 0;
}

int main(int argc, char *argv[])
{
	char user_input[INPUTSIZE], input_copy[INPUTSIZE];
	int i, count = 0;
	char *token;
	char delim[2] = " "; 
	char **parameters;

	printf("Welcome to Text ChatRoullette v0.1\n");

	while (1) {
	    printf("> ");
		fgets(user_input, INPUTSIZE - 1, stdin);
		user_input[strlen(user_input) - 1] = '\0';
		for (i = 0; user_input[i] != '\0'; i++) {
			user_input[i] = tolower(user_input[i]);
		}

		// parse user input
		strcpy(input_copy, user_input);
		token = strtok(input_copy, delim);
		if (strcmp(token, CONNECT) == 0) { /* connect */
			while (token != NULL) {
				parameters[count] = token;
				token = strtok(NULL, delim);
				if (token != NULL)
					count++;
			}
			if (count != 2) {
				printf("Connect only accpet 2 parameters: connect hostname port\n");
				continue;
			}
			connect_to_server(parameters[1], parameters[2]);
		} else if (strcmp(token, QUIT) == 0) {
            exit(1);
		}

	}

	return 0;
}
