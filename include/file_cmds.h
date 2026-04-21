#ifndef _FILE_CMDS_H_
#define _FILE_CMDS_H_
#include "protocol.h"
void handle_cd(int client_fd, ClientContext *ctx, char *arg);
void handle_ls(int client_fd, ClientContext *ctx);
void handle_pwd(int client_fd, ClientContext *ctx);
void handle_touch(int client_fd, ClientContext *ctx, char *arg);
void handle_rm(int client_fd, ClientContext *ctx, char *arg);
void handle_mkdir(int client_fd, ClientContext *ctx, char *arg);
void handle_rmdir(int client_fd, ClientContext *ctx, char *arg);

#endif