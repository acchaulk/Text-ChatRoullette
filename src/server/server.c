/*
 ** server.c -- a stream socket server demo
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
#include <signal.h>

#include "client.h"
#include "control_msg.h"

#define PORT "3490"  // the port users will be connecting to

#define CLIENT_MAX 10     // how many pending connections queue will hold
#define CHANNEL_MAX 5     // how many channels will exist concurrently

void sigchld_handler(int s) {
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Create a socket and listen on it
// return sockfd if success, otherwise client will exit. 
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

	return sockfd;
}

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

/* return 0 if connection sets up, otherwise return -1 */
int handle_new_connection(int sockfd, int *fdmax, fd_set *master,
		struct client_info *clients [], int *client_num, fd_set *bitmap) {
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
		FD_SET(new_fd, master); // add to master set
		if (new_fd > *fdmax) {
			*fdmax = new_fd; // keep track of the max
		}
		inet_ntop(their_addr.ss_family,
				get_in_addr((struct sockaddr *)&their_addr),
				remoteIP, sizeof remoteIP);
		printf("selectserver: new connection from %s on "
			   "socket %d\n", remoteIP, new_fd);
		
		// Acks client and increment current index
		return send_ack(new_fd, clients, client_num, bitmap);
	}
}

// Acks client and create a queue to keep track of clients
// TODO: synchronization
int send_ack(int sockfd, struct client_info * clients[], int *client_num, fd_set *bitmap) {
	struct client_info *node = NULL;
	char *buf;
	char ackbuf[BUF_MAX];
	int i;

	// use an array to manage the clients
	buf = malloc(10);
	node = malloc(sizeof(struct client_info));
	for (i = 0; i < CLIENT_MAX; i++) {
        if (!FD_ISSET(i, bitmap)) {
			//find a empty slot
			FD_SET(i, bitmap);
			break;
		}
	}
	memset(&ackbuf, 0, BUF_MAX);
	if (i == CLIENT_MAX) {
		sprintf(ackbuf, "Chat queue is full, please retry later");
		if (send(sockfd, ackbuf, strlen(ackbuf), 0) == -1) {
			perror("ack fails");
			return -1;
		}
		return 0;
	}
	sprintf(buf, "user %d", i);
	node->name = buf;
	node->sockfd = sockfd;
	node->partner_index = -1;
	clients[i] = node;

	sprintf(ackbuf, "%s:%s", ACK, node->name);
	if (send(sockfd, ackbuf, strlen(ackbuf), 0) == -1) {
		perror("ack fails");
		return -1;
	}
	(*client_num)++;
	return 0;
}

struct client_info* find_partner(int sockfd,
		struct client_info *clients[], int ci_size, fd_set *bitmap)
{
    int i, r;
    struct client_info *self = NULL;

    // find self
    for (i = 0; i < CLIENT_MAX; i++) {
	    if (FD_ISSET(i, bitmap)) {
            if (clients[i]->sockfd == sockfd) {
				self = clients[i];
				break;
			}
        }
    }

    // only one user at the time
    if (ci_size == 1) {
        self->partner_index = -1;
        return self;
    }

    // find a random parter (other than himself)
    srand(clock());
	do {
    	r = rand() % CLIENT_MAX;
    }
    while (r == i || !FD_ISSET(r, bitmap));
	self->partner_index = r;
	clients[r]->partner_index = i;

    return self;
}

struct client_info * handle_client_data(int sockfd, fd_set *master,
		struct client_info *clients [], int client_num,
		fd_set *bitmap)
{
	int nbytes;
	char buf[BUF_MAX]; // buffer for client data
	struct client_info *client;
	struct client_info *partner;

	if ((nbytes = recv(sockfd, buf, sizeof buf, 0)) <= 0) {
		// got error or connection closed by client
		if (nbytes == 0) {
			// connection closed
			printf("selectserver: socket %d hung up\n", sockfd);
		} else {
			perror("recv() client data fails");
		}
		close(sockfd); // bye!
		FD_CLR(sockfd, master); // remove from master set
		return NULL;
	} else {
		buf[nbytes] = '\0';
		if (strcmp(buf, CHAT_REQUEST) == 0) {
			// find a random partner and connect with the client who send the quest
			client = find_partner(sockfd, clients, client_num, bitmap);
			if (client->partner_index == -1) {
				printf("%s want to chat but there is no other user right now.\n", client->name);
				return NULL;
			}
			partner = clients[client->partner_index];
			// send IN_SESSION command to both clients
			memset(&buf, 0, nbytes);
			sprintf(buf, "%s:%s", IN_SESSION, partner->name);
			if (FD_ISSET(client->sockfd, master)) {
				if (send(client->sockfd, buf, strlen(buf), 0) == -1) {
					perror("send IN_SESSION fails");
					return NULL;
				}
			}
			client->in_session = TRUE;
			memset(&buf, 0, nbytes);
			sprintf(buf, "%s:%s", IN_SESSION, client->name);
			if (FD_ISSET(partner->sockfd, master)) {
				if (send(partner->sockfd, buf, strlen(buf), 0) == -1) {
					perror("send IN_SESSION fails");
					return NULL;
				}
			}
			partner->in_session = TRUE;
			return partner;
		} else {
			printf("expected %s but recv invalid control message: %s \n", CHAT_REQUEST, buf);
			return NULL;
		}
	}
}

int main(void) {
    int listener_fd;
	int fdmax;
	fd_set master;   // master file descriptor list
	fd_set read_fds; // tmp file descriptor list for select
	fd_set bitmap;  // hack for chat channel
	int i, j;
	struct client_info* clients[CLIENT_MAX];
	int client_num = 0;  // # of clients currently log in

	// reap all dead processes
	cleanup();

	FD_ZERO(&master);    // clear the master and temp sets
	FD_ZERO(&read_fds);
	FD_ZERO(&bitmap);

    // create socket and listen on it 
	listener_fd = setup();

    // add the listener to the master set
    FD_SET(listener_fd, &master);

	// keep track of the biggest file descriptor
	fdmax = listener_fd;

	printf("server: waiting for connections...\n");

	while(1) {  
	    read_fds = master; // copy it
		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1 ) {
            perror("select() fails");
			exit(4);
		}

		// debug message
		printf("read_fds:\n");
		for (i = 0; i < 64; i ++) {
			if (FD_ISSET(i, &read_fds)) {
				printf("1 ");
			} else {
				printf("0 ");
			}
		}
		printf("\n");
		printf("master_fds:\n");
		for (i = 0; i < 64; i ++) {
			if (FD_ISSET(i, &master)) {
				printf("1 ");
			} else {
				printf("0 ");
			}
		}
		printf("\n");

		// run through the existing connections looking for data to read
		for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == listener_fd) {
                	// getting new incoming connection
                	handle_new_connection(listener_fd, &fdmax, &master, clients, &client_num, &bitmap);
				} else {
					// handling data from client
					struct client_info * client;
					for (j = 0; j < CLIENT_MAX; j++) {
						if (FD_ISSET(j, &bitmap)) {
							if (i == clients[j]->sockfd) {
								client = clients[j];
								break;
							}
						}
					}
					// if client not in session, server will allocate a partner first
					if (!client->in_session) {
						handle_client_data(i, &master, clients, client_num, &bitmap);
						break;
					}

					// forwarding packet from client to partner
					struct client_info * partner = clients[client->partner_index];
					int nbytes = 0;
					char buf[BUF_MAX];
					if ((nbytes = recv(client->sockfd, buf, sizeof buf, 0)) <= 0) {
						// got error or connection closed by client
						if (nbytes == 0) {
							// connection closed
							printf("selectserver: socket %d hung up\n", client->sockfd);
						} else {
							perror("recv() client data fails");
						}
						close(client->sockfd); // bye!
						FD_CLR(client->sockfd, &master); // remove from master set
						break;
					} else {
						if (send(partner->sockfd, buf, nbytes, 0) == -1) {
							perror("send");
							break;
						}
					}
				}
			}
		}
	}

	return 0;
}
