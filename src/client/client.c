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
#include <pthread.h>

#include "client.h"
#include "control_msg.h"

#define PORT "3490" // the port client will be connecting to 
state_t state = INIT;
char *partner_name = NULL;

//TODO: implement Ctrl+C signal handler function
//TODO: chat is not working right now

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* return sockfd if success, otherwise -1 */
int handle_connect(char *hostname, char *port) {
    // TODO: use hostname instead of ip address
	int sockfd, numbytes;
	char buf[BUF_MAX];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	fd_set fdset;
	struct timeval tv;
	char *token;


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
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
        return -1;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
    printf("client: connecting to %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    if ((numbytes = recv(sockfd, buf, BUF_MAX-1, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';

    token = strtok(buf, ":");
	if (strcmp(token, MSG_ACK) != 0) {
		printf("expected %s but recv invalid control message: %s \n", MSG_ACK, buf);
		return -1;
	}
	token = strtok(NULL, ":");
	if (token == NULL) {
		return -1;
	}
	state = CONNECTING;
	printf("Connect to server successfully. Your user name is %s. Type '%s' to start chatting\n", token, CHAT);
	return sockfd;

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

/* return 0 for success, otherwise -1 */
int handle_chat(int sockfd) {
	int numbytes;
	char buf[BUF_MAX];
	char *token;

    if (sockfd == -1) {
		printf("Error: You need connect to server first.\n");
		return -1;
	}
	if (send(sockfd, MSG_CHAT_REQUEST, strlen(MSG_CHAT_REQUEST), 0) == -1) {
        perror("send Chat request fails");
		return -1;
	}

	// wait server
    if ((numbytes = recv(sockfd, buf, BUF_MAX-1, 0)) == -1) {
		perror("recv IN_SESSION fails");
		exit(1);
	}
    buf[numbytes] ='\0';
    token = strtok(buf, ":");
    if (strcmp(token, MSG_IN_SESSION) != 0) {
    	printf("expected %s but recv invalid control message: %s \n", MSG_IN_SESSION, buf);
    	return -1;
    }
    token = strtok(NULL, ":");
    printf("receive in_session with %s\n", token);
    state = CHATTING;
	return 0;
}

int send_text(int sockfd, char * text) {
	if (sockfd == -1) {
		printf("Error: You need connect to server first.\n");
		return -1;
	}
	if (send(sockfd, text, strlen(text), 0) == -1) {
		perror("send text fails");
		return -1;
	}
	return 0;
}

void print_help() {
	printf("%-10s - connect to TRS server.\n", CONNECT);
	printf("%-10s - chat with a random client in the common chat channel.\n", CHAT);
	printf("%-10s - transfer file to current chatting partner.\n", TRANSFER);
	printf("%-10s - report to TRS server current chatting partner is misbehaving\n", FLAG);
	printf("%-10s - print help information.\n", HELP);
	printf("%-10s - quit current channel.\n", QUIT);
	printf("%-10s - quit client.\n", EXIT);
}

void sender_thread(void* args) {



}

void* receiver_thread(void* args) {
	int numbytes;
	char buf[BUF_MAX];
	char *token;
	int sockfd = *(int *)args;
	while(1) {
    	if ((numbytes = recv(sockfd, buf, BUF_MAX-1, 0)) == -1) {
			perror("recv IN_SESSION fails");
			exit(1);
		}
		buf[numbytes] ='\0';
		token = strtok(buf, ":");
		if (strcmp(token, MSG_IN_SESSION) == 0) {
			state = CHATTING;
			token = strtok(NULL, ":");
			partner_name = malloc(sizeof(token));
			strncpy(partner_name, token, sizeof(token));
			continue;
		}
		if (partner_name != NULL) {
			printf("<%s>: %s\n", partner_name, buf);
		} else {
			printf("<server>: %s\n", buf);
		}
    }
	return 0;
}

int handle_quit(int sockfd) {
	if (sockfd == -1) {
		printf("Error: You need connect to server first.\n");
		return -1;
	}
	if (send(sockfd, QUIT, strlen(QUIT), 0) == -1) {
		perror("send QUIT fails");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char user_input[BUF_MAX], input_copy[BUF_MAX];
	int i;
	int connected = 0; /* 0 means unconnected, 1 means connected*/
	int sockfd = -1;
	char *token;
	char delim[2] = " ";
	int count = 0;
    struct thread_info *tinfo;
    pthread_t sender, receiver;

    print_ascii_art();

    while (1) {
		printf("> "); // prompt
		fgets(user_input, BUF_MAX - 1, stdin);
		user_input[strlen(user_input) - 1] = '\0';
		for (i = 0; user_input[i] != '\0'; i++) {
			user_input[i] = tolower(user_input[i]);
		}

		// parse user input
		strcpy(input_copy, user_input);
		if (input_copy[0] == '/') {
			// control command
			token = strtok(input_copy, delim);
			if (strcmp(token, CONNECT) == 0) {
				/* connect */
				while (token != NULL) {
					token = strtok(NULL, delim);
					if (token != NULL)
						count++;
				}
				if (count != 1) {
					printf("Usage: %s [hostname]\n", CONNECT);
					continue;
				}
				sockfd = handle_connect(token, PORT);
			    pthread_create(&receiver, NULL, &receiver_thread, (void *)&sockfd);
			} else if (strcmp(token, CHAT) == 0) {
				/* chat */
				handle_chat(sockfd);
			} else if (strcmp(token, EXIT) == 0) {
				/* exit */
				exit(1);
			} else if (strcmp(token, QUIT) == 0) {
				/* quit current channel */
				handle_quit(sockfd);
			} else if (strcmp(token, HELP) == 0) {
				/* help */
				print_help();
				continue;
			} else {
				printf("%s: Command not found. Type '%s' for more information.\n", token, HELP);
				continue;
			}
		} else {
            if (state == CHATTING) {
            	send_text(sockfd, input_copy);
            } else {
            	// TODO: strip whitespace
            	if (strcmp(input_copy, "") == 0) {
            		continue;
            	}
            	printf("Invalid command. Type '%s' for more information.\n", HELP);
            }
			continue;
		}
	}





    return 0;
}
