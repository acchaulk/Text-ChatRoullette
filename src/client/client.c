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

state_t g_state = INIT;
int g_sockfd = 0;
char *g_partner_name = NULL;
char *g_client_name = NULL;

//TODO: implement Ctrl+C signal handler in both cliend and server
//TODO: chat is not working right now
//TODO: type a string more than 256 characters could be a issue
//TODO: all capital letter become lower case on the other end

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void* receiver_thread(void* args) {
	int numbytes;
	char *token;
	int sockfd = *(int *)args;
	while(1) {
		char * buf = malloc(BUF_MAX);
    	if ((numbytes = recv(sockfd, buf, BUF_MAX-1, 0)) == -1) {
			perror("recv IN_SESSION fails");
			exit(1);
		}
    	buf[numbytes] = '\0';

    	if (g_state == CONNECTING) {
			/* server returns [IN_SESSION:user_name] */
			char *token[PARAMS_MAX];
			char *str;
			int count = 0;
			while ((str = strsep(&buf, ":")) != NULL) {
				token[count++] = strdup(str);
			}

			if (strcmp(token[0], MSG_IN_SESSION) == 0) {
				g_state = CHATTING;
				g_partner_name = strdup(token[1]);
				printf("You are chatting with %s\n", g_partner_name);
			}
			continue;
    	}
    	/* skip empty message */
    	if (strcmp(buf, "") != 0) {
    		printf("\n<%s>: %s\n", g_partner_name, buf);
    	}
		free(buf);
    }
	return 0;
}

/* return sockfd if success, otherwise -1 */
int handle_connect(char *hostname, char *port) {
    // TODO: use hostname instead of ip address
	int sockfd, numbytes;
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

    char * buf = malloc(BUF_MAX);
    if ((numbytes = recv(sockfd, buf, BUF_MAX-1, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';

    /* server returns [ACK:user_name] */
    char *token[PARAMS_MAX];
    char *str;
    int count = 0;
    while ((token[count++] = strsep(&buf, ":")) != NULL);

	if (strcmp(token[0], MSG_ACK) != 0) {
		printf("expected %s but recv invalid control message: %s \n", MSG_ACK, buf);
		free(buf);
		return -1;
	}

	g_client_name = strdup(token[1]);
	g_state = CONNECTING;
	printf("Connect to server successfully. Your user name is %s. Type '%s' to start chatting\n", g_client_name, CHAT);
	free(buf);
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

void parse_control_command(char * cmd) {
	char *params[PARAMS_MAX];
	char *token;
	char delim[2] = " ";
	int count = 0;
	pthread_t sender, receiver;

	while ((token = strsep(&cmd, delim)) != NULL) {
		params[count] = strdup(token);
		count++;
	}

	switch (g_state) {
	case INIT:
		if (strcmp(params[0], CONNECT) == 0) {
			if (count != 2) {
				printf("Usage: %s [hostname]\n", CONNECT);
				return;
			}
			g_sockfd = handle_connect(params[1], PORT);
			pthread_create(&receiver, NULL, &receiver_thread, (void *)&g_sockfd);
		} else if (strcmp(params[0], CHAT) == 0) {
            printf("Error: You need connect to server first.\n");
		} else if (strcmp(params[0], TRANSFER) == 0) {
			printf("Error: You are not in a chat session\n");
		} else if (strcmp(params[0], QUIT) == 0) {
            printf("Error: You are not in a chat session\n");
		} else if (strcmp(params[0], EXIT) == 0) {
			exit(1);
		} else if (strcmp(params[0], HELP) == 0) {
			print_help();
		} else if (strcmp(params[0], FLAG) == 0) {
            //to be implemented
		} else {
			printf("%s: Command not found. Type '%s' for more information.\n", params[0], HELP);
		}
		break;
	case CONNECTING:
		if (strcmp(params[0], CONNECT) == 0) {
			printf("Error: You are already connected to the server\n");
		} else if (strcmp(params[0], CHAT) == 0) {
			handle_chat(g_sockfd);
		} else if (strcmp(params[0], TRANSFER) == 0) {
			printf("Error: You are not in a chat session\n");
		} else if (strcmp(params[0], QUIT) == 0) {
			printf("Error: You are not in a chat session\n");
		} else if (strcmp(params[0], EXIT) == 0) {
			exit(1);
		} else if (strcmp(params[0], HELP) == 0) {
			print_help();
		} else if (strcmp(params[0], FLAG) == 0) {
			//to be implemented
		} else {
			printf("%s: Command not found. Type '%s' for more information.\n", params[0], HELP);
		}
		break;
	case CHATTING:
		if (strcmp(params[0], CONNECT) == 0) {
			printf("Error: You are already connected to the server\n");
		} else if (strcmp(params[0], CHAT) == 0) {
			printf("Error: You are in a chat session, type '%s' to quit current session\n", QUIT);
		} else if (strcmp(params[0], TRANSFER) == 0) {
			// to be implemented
		} else if (strcmp(params[0], QUIT) == 0) {
			handle_quit(g_sockfd);
		} else if (strcmp(params[0], EXIT) == 0) {
			exit(1);
		} else if (strcmp(params[0], HELP) == 0) {
			print_help();
		} else if (strcmp(params[0], FLAG) == 0) {
			//to be implemented
		} else {
			printf("%s: Command not found. Type '%s' for more information.\n", params[0], HELP);
		}
		break;
	case TRANSFERING:
		if (strcmp(params[0], CONNECT) == 0) {
			printf("Error: You are already connected to the server\n");
		} else if (strcmp(params[0], CHAT) == 0) {
			printf("Error: You are in a chat session, type '%s' to quit current session\n", QUIT);
		} else if (strcmp(params[0], TRANSFER) == 0) {
			// to be implemented
		} else if (strcmp(params[0], QUIT) == 0) {
			handle_quit(g_sockfd);
		} else if (strcmp(params[0], EXIT) == 0) {
			exit(1);
		} else if (strcmp(params[0], HELP) == 0) {
			print_help();
		} else if (strcmp(params[0], FLAG) == 0) {
			//to be implemented
		} else {
			printf("%s: Command not found. Type '%s' for more information.\n", params[0], HELP);
		}
		break;
	default:
		printf("This line should never be printed out\n");
		break;
	}
}

// strip whitespace
char *strip(char *s)
{
    size_t size;
    char *end;
    size = strlen(s);
    if (!size) return s;
    end = s + size - 1;
    while (end >= s && isspace(*end)) {
    	end--;
    }
    *(end + 1) = '\0';
    while (*s && isspace(*s)) {
    	s++;
    }
    return s;
}

int main(int argc, char *argv[])
{
	char user_input[BUF_MAX];
	char * input_copy;
	int i;
	int connected = 0; /* 0 means unconnected, 1 means connected*/
    struct thread_info *tinfo;

    print_ascii_art();

    while (1) {
		printf("%s> ", g_client_name == NULL ? "" : g_client_name); // prompt
		fgets(user_input, BUF_MAX - 1, stdin);
		user_input[strlen(user_input) - 1] = '\0';
		for (i = 0; user_input[i] != '\0'; i++) {
			user_input[i] = tolower(user_input[i]);
		}

		input_copy = strdup(user_input); // copy user input
		if (input_copy[0] == '/') {
			parse_control_command(input_copy);
			continue;
		} else {
			if (strcmp(strip(input_copy), "") == 0) {
				continue;
			}
            if (g_state == CHATTING) {
            	send_text(g_sockfd, input_copy);
            } else {
            	printf("%s: Command not found. Type '%s' for more information.\n", input_copy, HELP);
            }
			continue;
		}
	}





    return 0;
}
