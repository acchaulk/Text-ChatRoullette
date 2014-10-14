/*
 * server.c - Chat server for the Text ChatRoullette program
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <pthread.h>

#include "common.h"
#include "control_msg.h"

/* global variables for the server */
server_state_t g_state =  SERVER_INIT;
struct client_info* g_clients[CLIENT_MAX]; // chat queue
fd_set g_bitmap;  // bitmap for chat channel
fd_set g_master;  // global socket map
long g_useid = 0;  // global user id
pthread_t g_connector;

/* used by cleanup() to handle children */
void sigchld_handler(int s) {
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

/* get sockaddr, IPv4 or IPv6 */
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* Create a socket and listen on it
 * return sockfd if success, otherwise client will exit. */
int setup() {
	struct addrinfo hints, *servinfo, *p;
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	int rv;
	int yes=1;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
					sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}
		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, CLIENT_MAX) == -1) {
		perror("listen");
		exit(1);
	}

	printf("start TRS server successfully\n");

	return sockfd;
}

/* cleans up current processes */
void cleanup() {
	struct sigaction sa;
	
	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
}

/* Generate a new client node */
int create_client(int sockfd, struct client_info **node) {
	char *name;
	int index;

	//find a empty slot in chat queue
	for (index = 0; index < CLIENT_MAX; index++) {
		if (!FD_ISSET(index, &g_bitmap)) {
			FD_SET(index, &g_bitmap);
			break;
		}
	}

	// chat queue is full
	if (index == CLIENT_MAX) {
		char msg[] = "Chat queue is full, please retry later";
		if (send(sockfd, msg, strlen(msg), 0) == -1) {
			perror("chat queue full fails");
		}
		return -1;
	}

	name = malloc(NAME_LENGTH);
	*node = malloc(sizeof(struct client_info));

	sprintf(name, "user_%ld", g_useid++);

	(*node)->name = name;
	(*node)->sockfd = sockfd;
	(*node)->partner_index = -1;
	(*node)->state = CONNECTING;
	(*node)->blocked = 0;
	(*node)->flag = 0;

	return index;
}

/* destroys the current client */
void destroy_client(struct client_info ** client) {
	free((*client)->name);
	(*client)->name = NULL;
	free(*client);
	*client = NULL;
}

/* add client to chat queue, then ack back */
int send_ack(int sockfd, struct client_info * clients[], fd_set *bitmap) {
	char ack[BUF_MAX];
	struct client_info *client;

	/* add new client to chat queue */
	int index = create_client(sockfd, &client);
	if (index == -1) {
		return -1;
	}
	clients[index] = client;

	sprintf(ack, "%s:%s", MSG_ACK, client->name);
	if (send(sockfd, ack, strlen(ack), 0) == -1) {
		perror("ack fails");
		return -1;
	}
	return 0;
}

/* finds a chat partner for the client */
struct client_info* find_partner(int sockfd,
		struct client_info *clients[], fd_set *bitmap)
{
    int i, r;
    int avail_count = 0;
    int client_num = 0;
    int himself;
    struct client_info *self = NULL;
    int available_indices[CLIENT_MAX];

    // find himself and available indices
    memset(available_indices, 0, CLIENT_MAX);
    for (i = 0; i < CLIENT_MAX; i++) {
	    if (FD_ISSET(i, bitmap)) {
            client_num++;
            if (clients[i]->sockfd == sockfd) {
				self = clients[i];
				himself = i;
			} else {
				if (clients[i]->partner_index == -1) {
					available_indices[avail_count] = i;
					avail_count++;
				}
			}
        }
    }
    if (self->blocked) {
    	char msg[] = "Blocked user is not allowed to start a new chat";
    	if (send(self->sockfd, msg, strlen(msg), 0) == -1) {
			perror("send block fails");
		}
		return NULL;
    }

    // only one user at the time
    if (client_num == 1) {
        self->partner_index = -1;
        char msg[] =  "You are the only user in the system right now.";
		if (send(self->sockfd, msg, strlen(msg), 0) == -1) {
			perror("send fails");
		}
        return NULL;
    }

    if (avail_count == 0) {
    	char msg[] = "All users are chatting now, please try later.";
		if (send(sockfd, msg, strlen(msg), 0) == -1) {
			perror("no available fails");
		}
		return NULL;
    }

    // find a random parter (other than himself)
    srand(clock());
    r = rand() % avail_count; // not uniformly distributed
	if (r > avail_count - 1) {
		printf("Error: Partner(index:%d) is not in chat queue", r);
		return NULL;
	}
	int index = available_indices[r];
	self->partner_index = index;
	clients[index]->partner_index = himself;

    return self;
}

/* return 0 if connection sets up, otherwise return -1 */
int handle_new_connection(int sockfd, int *fdmax, fd_set *master,
		struct client_info *clients [], fd_set *bitmap) {
    int new_fd;
	socklen_t addrlen;
	struct sockaddr_storage their_addr; // connector's address information
	char remoteIP[INET6_ADDRSTRLEN];

	addrlen = sizeof their_addr;
	new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
					&addrlen);
	if (new_fd == -1) {
		perror("accept() fails");
		return -1;
	} else {
		FD_SET(new_fd, master); // add to g_master set
		if (new_fd > *fdmax) {
			*fdmax = new_fd; // keep track of the max
		}
		inet_ntop(their_addr.ss_family,
				get_in_addr((struct sockaddr *)&their_addr),
				remoteIP, sizeof remoteIP);
		printf("selectserver: new connection from %s on "
			   "socket %d\n", remoteIP, new_fd);

		// Acks client and increment current index
		return send_ack(new_fd, clients, bitmap);
	}
}

/* handler for chat requests */
struct client_info * handle_chat_request(int sockfd, fd_set *master,
		struct client_info *clients [], fd_set *bitmap)
{
	char buf[BUF_MAX]; // buffer for client data
	struct client_info *client;
	struct client_info *partner;

	// find a random partner and connect with the client who send the quest
	client = find_partner(sockfd, clients, bitmap);
	if (!client) {
		return NULL;
	}
	partner = clients[client->partner_index];
	// send IN_SESSION message to both clients
	memset(&buf, 0, BUF_MAX);
	sprintf(buf, "%s:%s", MSG_IN_SESSION, partner->name);
	if (FD_ISSET(client->sockfd, master)) {
		if (send(client->sockfd, buf, strlen(buf), 0) == -1) {
			perror("send IN_SESSION fails");
			return NULL;
		}
	}
	client->state = CHATTING;
	memset(&buf, 0, BUF_MAX);
	sprintf(buf, "%s:%s", MSG_IN_SESSION, client->name);
	if (FD_ISSET(partner->sockfd, master)) {
		if (send(partner->sockfd, buf, strlen(buf), 0) == -1) {
			perror("send IN_SESSION fails");
			return NULL;
		}
	}
	partner->state = CHATTING;
	return partner;
}

/* handler for transfering files */
void handle_transfer(const char * file_name, struct client_info *client, struct client_info *partner) {

	char buf[BUF_MAX];
	sprintf(buf, "%s:%s", MSG_RECEIVING_FILE, file_name);
	if (send(partner->sockfd, buf, sizeof(buf), 0) == -1) {
		perror("send receiving file fails");
		return;
	}

	if (send(client->sockfd, MSG_TRANSFER_ACK, sizeof(MSG_TRANSFER_ACK), 0) == -1) {
		perror("send reponse ack fails");
		return;
	}
	client->state = TRANSFERING;
	partner->state = TRANSFERING;
}

/* handler for the help command */
void handle_help(struct client_info *client) {
	char buf[512];
	sprintf(buf, "%-10s - connect to TRS server.\n"
			"%-10s - chat with a random client in the common chat channel.\n"
			"%-10s - transfer file to current chatting partner.\n"
			"%-10s - report to TRS server current chatting partner is misbehaving\n"
			"%-10s - print help information.\n"
			"%-10s - quit current channel.\n"
			"%-10s - quit client.\n",
			CONNECT, CHAT, TRANSFER, FLAG, HELP, QUIT, EXIT);
	if (send(client->sockfd, buf, strlen(buf), 0) == -1) {
		perror("send help message fails");
	}
}

/* prints all help commands */
void print_help() {
	printf("%-10s - list following information: \n"
			"\t\t[1] number of clients in chat queue,\n"
			"\t\t[2] number of clients chatting currently,\n"
			"\t\t[3] data usage for each channel being used,\n"
			"\t\t[4] total number of users flagged chatting partners and their names and their status.\n", STATS);
	printf("%-10s - kick out specific client from current channel.\n", THROWOUT);
	printf("%-10s - block specific client from starting a chat.\n", BLOCK);
	printf("%-10s - unblock specific client from ban list.\n", UNBLOCK);
	printf("%-10s - start server.\n", START);
	printf("%-10s - stop server with a grace period.\n", END);
	printf("%-10s - print help information.\n", HELP);
}

/* handler for a client exiting the program */
void handle_exit(struct client_info * client,
	struct client_info *partner, fd_set *bitmap) {
	FD_CLR(client->partner_index, bitmap);
	client->partner_index = -1;
	if (!partner) {
		partner->partner_index = -1;
	}
	free(client);
}

/* handler for the client quitting the current chat channel */
void handle_quit(struct client_info *client, struct client_info *partner) {
	partner->partner_index = -1;
	client->partner_index = -1;
	client->state = CONNECTING;
	partner->state = CONNECTING;
	if (send(partner->sockfd, MSG_QUIT, sizeof(MSG_QUIT), 0) == -1) {
		perror("quit channel fails");
	}
	if (send(client->sockfd, MSG_QUIT, sizeof(MSG_QUIT), 0) == -1) {
		perror("quit channel fails");
	}
}

void handle_flag(struct client_info * partner) {
	partner->flag++;
	char msg[] = "Your partner reported your misbehaving to the server";
	if (send(partner->sockfd, msg, strlen(msg), 0) == -1) {
		perror("quit channel fails");
	}
}

/* write stat to a file */
void handle_stat() {
	int i;
	int client_num = 0; /* number of clients in chat queue*/
	int chatter_num = 0; /* number of clients chatting currently */
	int total_flag = 0; /* total number of users flagged chatting partner */
	struct client_info *client;
	char status[30];

	FILE *fp = fopen(STAT_FILEPATH, "w");
	if (!fp) {
		perror("create stat file fails");
		return;
	}

	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			client = g_clients[i];

			client_num++;
			if (client->partner_index != -1) {
				chatter_num++;
			}
			if (client->flag != 0) {
				total_flag++;
			}
		}
	}
	int ret = fprintf(fp, "Number of clients in chat queue: %d\n"
			"Number of clients chatting currently: %d\n"
			"Total number of users flagged chatting partner: %d\n",
			client_num, chatter_num, total_flag);
	if (ret < 0) {
		perror("write stat file fails");
		fclose(fp);
		return;
	}

	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			client = g_clients[i];
			if (client->flag != 0) {
				if (client->partner_index != -1) {
					sprintf(status, "chatting with %s\n", g_clients[client->partner_index]->name);
				} else {
					sprintf(status, "not chatting\n");
				}
				if (fprintf(fp, "%s: receive %d flag, %s", client->name, client->flag, status) < 0) {
					perror("write stat file fails");
					fclose(fp);
					return;
				}
			}
		}
	}
	fclose(fp);
	printf("Write data to %s successfully\n", STAT_FILEPATH);
}

/* kick out specific user from current channel */
void handle_throwout(char * username) {
	int i;

	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			struct client_info * client = g_clients[i];
			if(strcmp(client->name, username) == 0) {
				if (client->partner_index != -1) {
					struct client_info * partner = g_clients[client->partner_index];

					client->partner_index = -1;
					client->state = CONNECTING;
					partner->partner_index = -1;
					partner->state = CONNECTING;

					if (send(client->sockfd, MSG_BE_KICKOUT, strlen(MSG_BE_KICKOUT), 0) == -1) {
						perror("kickout client fails");
					}
					if (send(partner->sockfd, MSG_PARTNER_BE_KICKOUT, strlen(MSG_PARTNER_BE_KICKOUT), 0) == -1) {
						perror("kickout partner fails");
					}
				} else {
					printf("%s is not chatting now", client->name);
				}
				return;
			}
		}
	}
	printf("'%s' is not found in chat queue\n", username);
}

/* handler for the blocking of a user */
void handle_block(char *username) {
	int i;
	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			struct client_info * client = g_clients[i];
			if(strcmp(client->name, username) == 0) {
				client->blocked = 1;
				if (send(client->sockfd, MSG_BLOCK, strlen(MSG_BLOCK), 0) == -1) {
					perror("block client fails");
				}
				return;
			}
		}
	}
	printf("'%s' is not found in chat queue\n", username);
}

/* handler for unblocking a user */
void handle_unblock(char *username) {
	int i;
	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			struct client_info * client = g_clients[i];
			if(strcmp(client->name, username) == 0) {
				client->blocked = 0;
				if (send(client->sockfd, MSG_UNBLOCK, strlen(MSG_UNBLOCK), 0) == -1) {
					perror("unblock client fails");
				}
				return;
			}
		}
	}
	printf("'%s' is not found in chat queue\n", username);
}

/* handler for ending the TRS*/
void handle_end(int signum)
{
	int i;
	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			struct client_info * client = g_clients[i];
			if (send(client->sockfd, MSG_SERVER_STOP, strlen(MSG_SERVER_STOP), 0) == -1) {
				perror("send end timer fails");
			}
			close(client->sockfd); // close socket();
			destroy_client(&client);

		}
	}
	FD_ZERO(&g_bitmap);
	pthread_kill(g_connector, SIGUSR1); // send a user define signal to kill thread
	g_state = SERVER_INIT;
	printf("Shutdown server successfully\n");
}

/* handler for when the server ends the TRS */
void handle_grace_period() {
	int i;
	char msg[] = "Server will be shutdown in 10 seconds!";
	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			struct client_info * client = g_clients[i];
			if (send(client->sockfd, MSG_GRACE_PERIOD, strlen(MSG_GRACE_PERIOD), 0) == -1) {
				perror("send grace period timer fails");
			}
		}
	}

	struct itimerval timer;
	struct sigaction sa;
	/* Install timer_handler as the signal handler for SIGVTALRM. */
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &handle_end;
	sigaction (SIGALRM, &sa, NULL);

	/* Configure the timer to expire after 10 second... */
	timer.it_value.tv_sec = GRACE_PERIOD_SECONDS;
	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &timer, NULL) < 0) {
		perror("set grace period timer fails");
		return;
	}
	g_state = GRACE_PERIOD;
	printf("Server will be shutdown in %d seconds!\n", GRACE_PERIOD_SECONDS);
}

/* forwards a message from the server to the partner */
int forward_message(struct client_info *partner, char *buf) {
	// forwarding packet from client to partner
	if (send_all_packets(partner->sockfd, buf, BUF_MAX) == -1) {
		perror("forward_chat_message");
		return -1;
	}
	printf("send '%s' to %s[socket %d]\n", buf, partner->name, partner->sockfd);
	return 0;
}

/* Helper function */
int send_all_packets(int socket, void *buffer, size_t length)
{
    char *ptr = (char*) buffer;
    while (length > 0)
    {
        int i = send(socket, ptr, length, 0);
        if (i < 1) {
        	return -1;
        }
        ptr += i;
        length -= i;
    }
    return 0;
}

/* sends the file the sockfd, input file */
int send_file(int sockfd, const char * input_file) {

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
		return 1;
	}

	/* Read data from file and send it */
	while (1) {
		/* First read file in chunks of 256 bytes */
		unsigned char buff[256] = { 0 };
		int nread = fread(buff, 1, 256, fp);
		printf("Bytes read %d \n", nread);

		/* If read was success, send data. */
		if (nread > 0) {
			printf("Sending \n");
			write(sockfd, buff, nread);
		}

		/*
		 * There is something tricky going on with read ..
		 * Either there was error, or we reached end of file.
		 */
		if (nread < 256) {
			if (feof(fp))
				printf("End of file\n");
			if (ferror(fp))
				printf("Error reading\n");
			break;
		}

	}

	return 0;
}

/* handler for file transfer completion */
void handle_transfer_complete(struct client_info *client, struct client_info *partner) {
	partner->state = CHATTING;
	client->state = CHATTING;
}

/* kills a current thread */
void kill_thread(int signum) {
	int i;
	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_master)) {
			close(i);
		}
	}
	FD_ZERO(&g_master);
	pthread_exit(NULL);
}

void exit_server(int signum) {
	int i;
	for (i = 0; i < CLIENT_MAX; i++) {
		if (FD_ISSET(i, &g_bitmap)) {
			struct client_info *client = g_clients[i];
			if (client->state > INIT) {
				printf("send exit_server to %s\n", client->name);
				if (send(client->sockfd, MSG_SERVER_SHUTDOWN, strlen(MSG_SERVER_SHUTDOWN), 0) == -1) {
					perror("notify client fails");
				}
			}
		}
	}
	printf("exit_server\n");
	exit(1);
}

/* main loop to be executed, handles the state transition */
void * main_loop(void * arg) {
    int listener_fd;
	int fdmax;
	fd_set master;   // master file descriptor list
	fd_set read_fds; // tmp file descriptor list for select
	int i, j;
	pthread_t connector, receiver;

	FD_ZERO(&master);    // clear the master and temp sets
	FD_ZERO(&read_fds);
	FD_ZERO(&g_bitmap);

    // create socket and listen on it
	listener_fd = setup();

	g_state = SERVER_RUNNING;

    // add the listener to the g_master set
    FD_SET(listener_fd, &master);

	// keep track of the biggest file descriptor
	fdmax = listener_fd;

	struct sigaction sa;
	/* Install timer_handler as the signal handler for SIGVTALRM. */
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &exit_server;
	sigaction (SIGINT, &sa, NULL);

	while(1) {  
	    read_fds = master; // copy it
		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1 ) {
            perror("select() fails");
			exit(4);
		}

		// run through the existing connections looking for data to read
		for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == listener_fd) {
                	// getting new incoming connection
                	handle_new_connection(listener_fd, &fdmax, &master, g_clients, &g_bitmap);
				} else {
					// handling data from client
					struct client_info * client;
					for (j = 0; j < CLIENT_MAX; j++) {
						if (FD_ISSET(j, &g_bitmap)) {
							if (i == g_clients[j]->sockfd) {
								client = g_clients[j];
								break;
							}
						}
					}

					int nbytes;
				    char *buf = malloc(BUF_MAX + 1); // buffer for client data
					if ((nbytes = recv(i, buf, BUF_MAX, 0)) <= 0) {
						// got error or connection closed by client
						if (nbytes == 0) {
							// connection closed
							printf("selectserver: socket %d hung up\n", i);
						} else {
							perror("recv() client data fails");
						}
						close(i); // bye!
						FD_CLR(i, &master); // remove from g_master set
						continue;
					} else {
						buf[nbytes + 1] = '\0';
						printf("receive '%s' from %s[socket %d]\n", buf, client->name, client->sockfd);

						char *token;
						char *params[PARAMS_MAX];
						int count = 0;

						while ((token = strsep(&buf, ":")) != NULL) {
							params[count] = strdup(token);
							count++;
						}

						/* handle help first */
						if (strcmp(params[0], HELP) == 0) {
							print_help();
							continue;
						}

						switch (client->state) {
						case INIT:
							break;
						case CONNECTING:
							if (strcmp(params[0], EXIT) == 0) {
								handle_exit(client, NULL, &g_bitmap);
							} else if (strcmp(params[0], MSG_HELP) == 0) {
								handle_help(client);
							} else if (strcmp(params[0], MSG_CHAT_REQUEST) == 0) {
								// if client request to chat, server will allocate a partner first
								handle_chat_request(i, &master, g_clients, &g_bitmap);
								break;
							}
							break;
						case CHATTING:
						{
                            struct client_info *partner = g_clients[client->partner_index];
                            if (strcmp(params[0], EXIT) == 0) {
                            	handle_exit(client, partner, &g_bitmap);
                            } else if (strcmp(params[0], QUIT) == 0) {
								handle_quit(client, partner);
                            } else if (strcmp(params[0], MSG_HELP) == 0) {
								handle_help(client);
							} else if (strcmp(params[0], MSG_FLAG) == 0){
                            	handle_flag(partner);
                            } else if (strcmp(params[0], MSG_SENDING_FILE) == 0) {
                            	handle_transfer(params[1], client, partner);
                            } else {
                            	forward_message(partner, params[0]);
                            }
							break;
						}
						case TRANSFERING:
						{
							struct client_info *partner = g_clients[client->partner_index];
							if (strcmp(params[0], MSG_RECEIVE_SUCCESS) == 0) {
								handle_transfer_complete(client, partner);
							} else if (strcmp(params[0], MSG_HELP) == 0) {
								handle_help(client);
							} else {
								forward_message(partner, params[0]);
							}
							break;
						}
						default:
							break;
						}
						int k;
						for(k = 0; k < count; k++) {
							free(params[k]);
						}
					}
					free(buf);
				}
			}
		}
	}
	return 0;
}

/* parses control commands entered by the admin */
void parse_control_command(char * cmd) {
	char *params[PARAMS_MAX];
	char *token;
	char delim[2] = " ";
	int count = 0;

	while ((token = strsep(&cmd, delim)) != NULL) {
		params[count] = strdup(token);
		count++;
	}

	switch (g_state) {
	case SERVER_INIT:
		if (strcmp(params[0], STATS) == 0    ||
			strcmp(params[0], THROWOUT) == 0 ||
			strcmp(params[0], BLOCK) == 0    ||
			strcmp(params[0], UNBLOCK) == 0) {
			printf("You need start server first\n");
		} else if (strcmp(params[0], START) == 0) {
			pthread_create(&g_connector, NULL, &main_loop, NULL);
		} else if (strcmp(params[0], END) == 0) {
			/* server has not started yet, don't need grace period */
			printf("Server hasn't started yet\n");
		} else if (strcmp(params[0], HELP) == 0) {
			print_help();
		} else {
			printf("%s: Command not found. Type '%s' for more information.\n", params[0], HELP);
		}
		break;
	case SERVER_RUNNING:
		if (strcmp(params[0], STATS) == 0) {
			handle_stat();
		} else if (strcmp(params[0], CHAT) == 0) {
			// TODO: Should admin able to talk with other clients?
		} else if (strcmp(params[0], THROWOUT) == 0) {
			if (count != 2) {
				printf("Usage: %s [username]\n", THROWOUT);
				return;
			}
			handle_throwout(params[1]);
		} else if (strcmp(params[0], BLOCK) == 0) {
			if (count != 2) {
				printf("Usage: %s [username]\n", BLOCK);
				return;
			}
			handle_block(params[1]);
		} else if (strcmp(params[0], UNBLOCK) == 0) {
			if (count != 2) {
				printf("Usage: %s [username]\n", UNBLOCK);
				return;
			}
			handle_unblock(params[1]);
		} else if (strcmp(params[0], START) == 0) {
			printf("Server has already started.\n");
		} else if (strcmp(params[0], END) == 0) {
			handle_grace_period();
		} else if (strcmp(params[0], HELP) == 0) {
			print_help();
		} else {
			printf("%s: Command not found. Type '%s' for more information.\n", params[0], HELP);
		}
		break;
	case GRACE_PERIOD:
		break;
	default:
		break;
	}

}

/* main function */
int main(void) {
    int listener_fd;
	int fdmax;
	fd_set master;   // master file descriptor list
	fd_set read_fds; // tmp file descriptor list for select
	fd_set bitmap;  // hack for chat channel
	int i, j;
	struct client_info* clients[CLIENT_MAX];
	int client_num = 0;  // # of clients currently log in
	pthread_t connector, receiver;
	char user_input[BUF_MAX];

	// reap all dead processes
//	cleanup();

	print_ascii_art();

	while (1) {
		printf("admin> "); // prompt
		fgets(user_input, BUF_MAX, stdin);
		user_input[strlen(user_input) - 1] = '\0';

		char * input_copy = strdup(user_input); // copy user input
		if (input_copy[0] == '/') {
			parse_control_command(input_copy);
		} else {
			if (strcmp(strip(input_copy), "") == 0) {
				continue;
			}
			printf("%s: Command not found. Type '%s' for more information.\n", input_copy, HELP);
		}
	}

	return 0;
}
