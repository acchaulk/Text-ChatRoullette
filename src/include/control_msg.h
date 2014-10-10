#ifndef __CONTROL_MSG_H__
#define __CONTROL_MSG_H__

#define MSG_ACK "Acknowledgment"
#define MSG_CHAT_REQUEST "chat_request"
#define MSG_IN_SESSION "in_session"

// supported commands
#define CONNECT "/connect"
#define CHAT "/chat"
#define TRANSFER "/transfer"
#define FLAG "/flag"
#define HELP "/help"
#define QUIT "/quit"
#define EXIT "/exit"

#define TRUE 1
#define FALSE 0

#define PORT "3490" // the port client will be connecting to
#define BUF_MAX 256 // max size for client data
#define CLIENT_MAX 10     // how many pending connections queue will hold
#define CHANNEL_MAX 5     // how many channels will exist concurrently
#define PARAMS_MAX 10 // maximum number of parameter

#endif
