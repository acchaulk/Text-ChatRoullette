#ifndef __CLIENT_H__
#define __CLIENT_H__

struct chat_pair {
   char *client_name;
   int client_index;
   char *partner_name;
   int partner_index;
   int channel;
   struct chat_pair *next;
};

struct client_info {
   char *name;
   int sockfd;
};

#define BUF_MAX 256 // max size for client data

#endif
