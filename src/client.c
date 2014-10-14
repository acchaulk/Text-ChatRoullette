/*
 * client.c - Client program that lets the user connect to the Text Chat Roullette
 * server and chat with other members
 */

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
#include <sys/stat.h>
#include <libgen.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "common.h"
#include "control_msg.h"

/* global variables for the client */
client_state_t g_state = INIT;
int g_sockfd = 0;
char *g_partner_name = NULL;
char *g_client_name = NULL;
FILE *g_FP;

/* get sockaddr, IPv4 or IPv6 */
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* handles messages recieved from the server and contains state machine for the client */
void* receiver_thread(void* args) {
	int numbytes;
	char *token[PARAMS_MAX];
	char *str;
	int sockfd = *(int *)args;
	int is_control_msg = 1; /* flag */

	while(1) {
		char * buf = malloc(BUF_MAX + 1);
    	if ((numbytes = recv(sockfd, buf, BUF_MAX, 0)) == -1) {
			perror("recv IN_SESSION fails");
			exit(1);
		}
    	buf[numbytes + 1] = '\0';

		int count = 0;
		while ((str = strsep(&buf, ":")) != NULL) {
			token[count++] = strdup(str);
		}
    	switch (g_state) {
    	case CONNECTING:
    		if (strcmp(token[0], MSG_SERVER_STOP) == 0 ||
					strcmp(token[0], MSG_SERVER_SHUTDOWN) == 0) {
				close(g_sockfd); // close server socket
				g_sockfd = 0;
				g_state = INIT;
				printf("Client quits because server shutdown\n");
				return NULL;
			} else if (strcmp(token[0], MSG_IN_SESSION) == 0) {
				/* server returns [IN_SESSION:user_name] */
				g_state = CHATTING;
				g_partner_name = strdup(token[1]);
				printf("You are chatting with %s\n", g_partner_name);
			} else if (strcmp(token[0], MSG_BLOCK) == 0) {
				printf("You are banned to start a new chat by admin");
			} else if (strcmp(token[0], MSG_UNBLOCK) == 0) {
				printf("Your name is removed from block list");
			} else if (strcmp(token[0], MSG_GRACE_PERIOD) == 0) {
				printf("Server will be shutdown in 10 seconds!\n");
			} else {
				is_control_msg = 0;
			}
			break;
    	case CHATTING:
    		if (strcmp(token[0], MSG_SERVER_STOP) == 0 ||
					strcmp(token[0], MSG_SERVER_SHUTDOWN) == 0) {
				close(g_sockfd); // close server socket
				g_sockfd = 0;
				g_state = INIT;
				printf("Client quits because server shutdown\n");
				return NULL;
			}
    		else if (strcmp(token[0], MSG_QUIT) == 0) {
    			g_state = CONNECTING;
    			printf("You quit your current chat channel\n");
    		} else if (strcmp(token[0], MSG_BE_KICKOUT) == 0) {
    			g_state = CONNECTING;
    			printf("You are kicked out from current channel by admin\n");
    		} else if (strcmp(token[0], MSG_PARTNER_BE_KICKOUT) == 0) {
    			g_state = CONNECTING;
    			printf("Your partner be kicked out from current channel by admin\n");
    		} else if (strcmp(token[0], MSG_BLOCK) == 0) {
    			g_state = CONNECTING;
    			printf("You are banned to start a new chat by admin");
    		} else if (strcmp(token[0], MSG_TRANSFER_ACK) == 0) {
    			g_state = TRANSFERING;
			} else if(strcmp(token[0], MSG_RECEIVING_FILE) == 0) {
    			g_state = TRANSFERING;

    			if(count != 2) {
    				printf("Incorrect file name\n");
    			}
    			open_file(token[1]);
    		} else if (strcmp(token[0], MSG_GRACE_PERIOD) == 0) {
				printf("Server will be shutdown in 10 seconds!\n");
			} else {
    			is_control_msg = 0;
    		}
    		break;
    	case TRANSFERING:
    		if (strcmp(token[0], MSG_SERVER_SHUTDOWN) == 0) {
    			close(g_sockfd); // close server socket
				g_sockfd = 0;
				g_state = INIT;
				printf("Client quits because server shutdown\n");
				return NULL;
    		} else {
    			if (count == 1) {
    				receive_file(token[0], NULL);
    			} else if (count == 2) {
    				receive_file(token[0], token[1]);
    			}
    		}
    		break;
    	default:
    		break;
    	}

    	/* skip empty message */
		if (!is_control_msg && strcmp(token[0], "") != 0) {
			printf("\n%s\n", token[0]);
		}
		int i;
		for (i = 0; i < count; i++) {
			free(token[i]);
		}
		free(buf);
    }
	return 0;
}

/* handler for the connnect command
 * return sockfd if success, otherwise -1 */
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
        printf("failed to connect server\n");
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
	printf("Connect to server successfully. Your user name is %s. Type '%s' to start chatting\n",
			g_client_name, CHAT);
	g_state = CONNECTING;
	free(buf);
	return sockfd;

}

/* handler for the chat command
 * return 0 for success, otherwise -1 */
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

/* sends text to the chat partner */
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

/* request help messages from server */
void request_help() {
	if (send(g_sockfd, MSG_HELP, sizeof(MSG_HELP), 0) == 0) {
		perror("send help request fails");
	}
}

/* handler for the client quitting the chat channel */
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

int handle_flag() {
	if (send(g_sockfd, MSG_FLAG, sizeof(MSG_FLAG), 0) == -1) {
		perror("send flag fails");
		return -1;
	}
	printf("send flag to server successfully\n");
	return 0;
}

/* handler for opening a file */
int open_file(const char * input_file) {

	int bytesReceived = 0;
	char recvBuff[BUF_MAX + 1];

	char filepath[BUF_MAX];
	sprintf(filepath, "recv/%s", input_file);
	g_FP = fopen(filepath, "w");
	if (NULL == g_FP) {
		printf("Error opening file");
		return -1;
	}

	return 0;
}

int receive_file(char * filebuf, char * msg_transfer_complete) {

	/* Receive data in chunks of 256 bytes */
	if (filebuf) {
		fwrite(filebuf, 1, strlen(filebuf), g_FP);
		/* search for completion flag inside chunk */
		if (msg_transfer_complete) {
			fclose(g_FP);
			g_FP = NULL;
			g_state = CHATTING;
			if (send(g_sockfd, MSG_RECEIVE_SUCCESS, strlen(MSG_RECEIVE_SUCCESS), 0) == -1) {
				perror("response receive success fails");
			}
			printf("File transfer success!\n");
		}
	} else {
		printf("\n Read Error \n");
	}

	return 0;
}

/* sends the file given the path name */
int send_file(const char * input_file) {

	/* check file size */
	struct stat st;
	stat(input_file, &st);
	int size = st.st_size; // size in bytes

	if (size > 10000000) {
		printf("Size of file > 100 MB. Must send a smaller file.");
		return -1;
	}

	/* Open the file that we wish to transfer */
	FILE *fp = fopen(input_file, "rb");
	if (fp == NULL) {
		printf("File open error");
		return -1;
	}
	char buf[BUF_MAX];
	char * file_name= strdup(input_file);
	sprintf(buf, "%s:%s", MSG_SENDING_FILE, basename(file_name));
	if(send(g_sockfd, buf, strlen(buf), 0) == -1) {
		printf("Could not send the file.\n");
	}

	/* wait for server response with 50 sec timeout */
	int loop = 0;
	if (g_state != TRANSFERING && loop < 100) {
		usleep(500);
		loop++;
	}

	if (g_state != TRANSFERING) {
		return -1;
	}

	/* Read data from file and send it */
	while (1) {
		/* First read file in chunks of 256 bytes */
		unsigned char buff[BUF_MAX] = { 0 };
		int nread = fread(buff, 1, BUF_MAX, fp);
		// printf("Bytes read %d \n", nread);

		/* If read was success, send data. */
		if (nread > 0) {
			// printf("Sending '%s'\n", buff);
			write(g_sockfd, buff, nread);
		}

		if (nread < BUF_MAX) {
			if (feof(fp)) {
				printf("End of file\n");
			} if (ferror(fp)) {
				printf("Error reading\n");
			} break;
		}

	}

	fclose(fp);
	if (send(g_sockfd, MSG_TRANSFER_COMPLETE, sizeof(MSG_TRANSFER_COMPLETE), 0) == -1) {
		perror("MSG_TRANSFER_COMPLETE fails");
	}

	g_state = CHATTING;

	return 0;
}

/* parses commands entered by the client */
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
			if (g_sockfd == -1) {
				return;
			}
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
			printf("Error: You need connect to server first.\n");
		} else if (strcmp(params[0], FLAG) == 0) {
			printf("Error: You are not in a chat session\n");
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
			request_help();
		} else if (strcmp(params[0], FLAG) == 0) {
			printf("Error: You are not in a chat session\n");
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
			if (count != 2) {
				printf("Usage: %s /this/is/a/file \n", TRANSFER);
				return;
			} if(send_file(params[1]) == 0) {
				printf("Sent file %s successfully! \n", params[1]);
			}
		} else if (strcmp(params[0], QUIT) == 0) {
			handle_quit(g_sockfd);
		} else if (strcmp(params[0], EXIT) == 0) {
			exit(1);
		} else if (strcmp(params[0], HELP) == 0) {
			request_help();
		} else if (strcmp(params[0], FLAG) == 0) {
			handle_flag();
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
			request_help();
		} else if (strcmp(params[0], FLAG) == 0) {
			handle_flag();
		} else {
			printf("%s: Command not found. Type '%s' for more information.\n", params[0], HELP);
		}
		break;
	default:
		printf("This line should never be printed out\n");
		break;
	}

}

/* main loop for the client program */
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
		fgets(user_input, BUF_MAX, stdin);
		user_input[strlen(user_input) - 1] = '\0';

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
