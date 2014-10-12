#ifndef __CONTROL_MSG_H__
#define __CONTROL_MSG_H__

#define MSG_ACK "##acknowledgment"
#define MSG_CHAT_REQUEST "##chat_request"
#define MSG_IN_SESSION "##in_session"
#define MSG_QUIT "##partner_quit"
#define MSG_ADMIN "##upgrade_to_admin"
#define MSG_BE_KICKOUT "##be_kicked_out"
#define MSG_PARTNER_BE_KICKOUT "##partner_be_kicked_out"
#define MSG_BLOCK "##be_blocked"
#define MSG_UNBLOCK "##be_unblocked"

// supported client commands
#define CONNECT "/connect"
#define CHAT "/chat"
#define TRANSFER "/transfer"
#define FLAG "/flag"
#define HELP "/help"
#define QUIT "/quit"
#define EXIT "/exit"

// supported admin command
#define STATS "/stats"
#define THROWOUT "/throwout"
#define BLOCK "/block"
#define UNBLOCK "/unblock"
#define START "/start"
#define END "/end"

#endif
