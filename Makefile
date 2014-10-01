#
# Makefile of Text ChatRoullette
#

SERVER_SRC := server/server.c

CLIENT_SRC := client/client.c

# Predefine directories
PWD := $(shell pwd;cd)
TOPDIR := $(PWD)
SRC_DIR := $(TOPDIR)/src
OBJ_DIR := $(TOPDIR)/obj
INCLUDE_DIR := $(SRC_DIR)/include
CLIENT_SRC_DIR := $(SRC_DIR)/client
SERVER_SRC_DIR := $(SRC_DIR)/server
CLIENT_OBJ_DIR := $(OBJ_DIR)/client
SERVER_OBJ_DIR := $(OBJ_DIR)/server

CLIENT_OBJ := $(patsubst %.c, $(OBJ_DIR)/%.o, $(CLIENT_SRC))
SERVER_OBJ := $(patsubst %.c, $(OBJ_DIR)/%.o, $(SERVER_SRC))

# debug info
#$(info SRC_DIR=$(SRC_DIR))
#$(info OBJ_DIR=$(OBJ_DIR))
#$(info CLIENT_OBJ_DIR=$(CLIENT_OBJ_DIR))
#$(info SERVER_OBJ_DIR=$(SERVER_OBJ_DIR))
#$(info CLIENT_OBJ=$(CLIENT_OBJ))
#$(info SERVER_OBJ=$(SERVER_OBJ))

LIBS :=

CLIENT_TARGET := client
SERVER_TARGET := server

CFLAGS := -g -I$(INCLUDE_DIR)

all: dir client server

dir:
	@mkdir -p $(CLIENT_OBJ_DIR)
	@mkdir -p $(SERVER_OBJ_DIR)

client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $(TOPDIR)/$(CLIENT_TARGET) $(CLIENT_OBJ) $(LIBS)

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $(TOPDIR)/$(SERVER_TARGET) $(SERVER_OBJ) $(LIBS)

clean:
	rm -rf $(OBJ_DIR) $(TOPDIR)/$(CLIENT_TARGET) $(TOPDIR)/$(SERVER_TARGET)

$(CLIENT_OBJ_DIR)/%.o : $(CLIENT_SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_OBJ_DIR)/%.o : $(SERVER_SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

