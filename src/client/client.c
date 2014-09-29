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
#define TRANSFER "transfer"
#define FLAG "flag"
#define HELP "help"
#define QUIT "quit"
#define EXIT "exit"

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

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
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

void print_ascii_art() {
	printf("\n");
	printf("Welcome to Chat Roullette v1.0\n");
	printf("\n");
    printf("╔═╗┬ ┬┌─┐┌┬┐  ╦═╗┌─┐┬ ┬┬  ┬  ┌─┐┌┬┐┌┬┐┌─┐\n");
	printf("║  ├─┤├─┤ │   ╠╦╝│ ││ ││  │  ├┤  │  │ ├┤ \n");
	printf("╚═╝┴ ┴┴ ┴ ┴   ╩╚═└─┘└─┘┴─┘┴─┘└─┘ ┴  ┴ └─┘\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	char user_input[INPUTSIZE], input_copy[INPUTSIZE];
	int i, count = 0;
	char *token;
	char delim[2] = " "; 
	char **parameters;

	print_ascii_art();

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
				printf("Usage: connect [hostname] [port]\n");
				continue;
			}
			connect_to_server(parameters[1], parameters[2]);
		} else if (strcmp(token, EXIT) == 0) { /* exit */
            exit(1);
		} else if (strcmp(token, HELP) == 0) { /* help */
		    printf("%-10s - connect to TRS server.\n", CONNECT);
		    printf("%-10s - chat with a random client in the common chat channel.\n", CHAT);
			printf("%-10s - transfer file to current chatting partner.\n", TRANSFER);
			printf("%-10s - report to TRS server current chatting partner is misbehaving\n", FLAG);
			printf("%-10s - print help information.\n", HELP);
			printf("%-10s - quit current channel.\n", QUIT);
			printf("%-10s - quit client.\n", EXIT);
			continue;
		} else {
            printf("%s: Command not found. Type 'help' for more information.\n", token);
			continue;
		}

	}

	return 0;
}
