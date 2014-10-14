/*
 * common.h - contains constants, functions, and structs used by the client and server
 */

#ifndef __COMMON_H__
#define __COMMON_H__

typedef enum { INIT, CONNECTING, CHATTING, TRANSFERING } client_state_t;
typedef enum { SERVER_INIT, SERVER_RUNNING,  GRACE_PERIOD } server_state_t;

#define PORT                   "3490" // the port client will be connecting to
#define BUF_MAX                256    // max size for client data
#define CLIENT_MAX             64     // how many pending connections queue will hold
#define PARAMS_MAX             10     // maximum number of parameter
#define NAME_LENGTH            10     // maximum characters for client name
#define GRACE_PERIOD_SECONDS   10     // grace period seconds for stopping the server

#define STAT_FILEPATH       "log/stat.txt"

/* represent client status on server side */
struct client_info {
   char *name;
   int sockfd;
   int partner_index;
   client_state_t state;
   int blocked; /*0 for not blocked, 1 for blocked */
   int flag; /* number of flags received */
};

void print_ascii_art();

/* strip leading and trailing whitespace */
char* strip(char *s);

#endif /* __COMMON_H__ */
