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
		struct client_info *ci_list [], int *ci_size) {
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
		return send_ack(new_fd, ci_list, ci_size);
	}
}

// Acks client and create a queue to keep track of clients
// TODO: synchronization
int send_ack(int sockfd, struct client_info *ci_list [], int *ci_size) {
	struct client_info *node = NULL;
	char *buf;
	char ackbuf[BUF_MAX];

	// use an array to manage the clients
	buf = malloc(10);
	node = malloc(sizeof(struct client_info));
	sprintf(buf, "user%d", *ci_size);
	node->name = buf;
	node->sockfd = sockfd;
	ci_list[*ci_size] = node;
	(*ci_size)++;

	memset(&ackbuf, 0, BUF_MAX);
	sprintf(ackbuf, "%s:%s", ACK, node->name);
	if (send(sockfd, ackbuf, strlen(ackbuf), 0) == -1) {
		perror("ack fails");
		return -1;
	}
	return 0;
}

struct chat_pair* find_partner(int sockfd,
		struct client_info *ci_list[], int ci_size,
		struct chat_pair *cp_list[], fd_set *channel) {
    int i, r;
    struct client_info *self = NULL;
    struct chat_pair *cp = malloc(sizeof(struct chat_pair));

    // find self
    for (i = 0; i < ci_size; i++) {
        if (ci_list[i]->sockfd == sockfd) {
            self = ci_list[i];
            cp->client_index = i;
            cp->client_name = self->name;
            break;
        }
    }
    // only one user at the time
    if (ci_size == 1) {
        cp->partner_index = -1;
        cp->partner_name = NULL;
        return cp;
    }

    // find a random parter (other than himself)
    srand(clock());
    r = rand() % ci_size;
    while (r != i) {
    	r = rand() % ci_size;
    }
    cp->partner_index = r;
    cp->partner_name = ci_list[r]->name;
    // find an available channel
    for (i = 0; i < CHANNEL_MAX; i++) {
    	if (!FD_ISSET(i, channel)) {
    		cp->channel = i;
    		FD_SET(i, channel);
    		break;
    	}
    }
    // add to global list
    cp_list[cp->channel] = cp;

    return cp;
}

int handle_client_data(int sockfd, fd_set *master,
		struct client_info *ci_list [], int ci_size,
		struct chat_pair *cp_list [], fd_set *channel) {
	int nbytes;
	char buf[BUF_MAX]; // buffer for client data
	struct chat_pair *cp;

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
		return -1;
	} else {
		buf[nbytes] = '\0';
		if (strcmp(buf, CHAT_REQUEST) == 0) {
			// find a random partner
			cp = find_partner(sockfd, ci_list, ci_size, cp_list, channel);
			if (cp->partner_index == -1) {
				printf("%s want to chat but there is no other user right now.\n", cp->client_name);
				return -1;
			}
			// send IN_SESSION command to both clients
			memset(&buf, 0, nbytes);
			sprintf(buf, "%s:%s", IN_SESSION, cp->client_name);
			if (send(ci_list[cp->client_index]->sockfd, buf, strlen(buf), 0) == -1) {
				perror("send IN_SESSION fails");
				return -1;
			}
			memset(&buf, 0, nbytes);
			sprintf(buf, "%s:%s", IN_SESSION, cp->partner_name);
			if (send(ci_list[cp->partner_index]->sockfd, buf, strlen(buf), 0) == -1) {
				perror("send IN_SESSION fails");
				return -1;
			}
		}
	}
}

int main(void) {
    int sockfd;
	int fdmax;
	fd_set master;   // master file descriptor list
	fd_set read_fds; // tmp file descriptor list for select
	int i, j;
	struct client_info* ci_list[CLIENT_MAX];
	int ci_size = 0;  // current # of client list
	struct chat_pair* cp_list[CHANNEL_MAX];
	fd_set channel;  // hack for chat channel

	// reap all dead processes
	cleanup();

    // create socket and listen on it 
	sockfd = setup();

    // add the listener to the master set
    FD_SET(sockfd, &master); 

	// keep track of the biggest file descriptor
	fdmax = sockfd;

	FD_ZERO(&channel);

	printf("server: waiting for connections...\n");

	while(1) {  
	    read_fds = master; // copy it
		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1 ) {
            perror("select() fails");
			exit(4);
		}

		// run through the existing connections looking for data to read
		for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == sockfd) {
                	handle_new_connection(sockfd, &fdmax, &master, ci_list, &ci_size);
				} else {
				    handle_client_data(i, &master, ci_list, ci_size, cp_list, &channel);
					/*
					for (j = 0; j <= fdmax; j++) {
						// send to everyone
						if (FD_ISSET(j, &master)) {
							// except the sock_fd and ourselves
							if (j != sockfd && j != i) {
								if (send(j, buf, nbytes, 0) == -1) {
									perror("send() fails");
								}
							}
						}
					}
					*/
				} // end of handling data from client
			} // end of getting new incoming connection
		} // end of looping through file descriptors

	} // end of while

	return 0;
}
