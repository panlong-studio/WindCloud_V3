#ifndef _HANDLE_H_
#define _HANDLE_H_

// 函数作用：给客户端发送一条普通文本消息。
// 参数 client_fd：客户端 socket。
// 参数 msg：要发送的字符串。
// 返回值：无。
void send_msg(int client_fd, const char *msg);

// 函数作用：处理一个客户端连接上的所有请求。
// 参数 listen_fd：这里实际上传入的是某个客户端连接 fd，不是监听 fd。
// 返回值：无。
void handle_request(int listen_fd);

// 函数作用：处理 cd 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径，会在成功进入目录后被修改。
// 参数 arg：用户输入的目录名。
// 返回值：无。
void handle_cd(int listen_fd,char *current_path,char *arg);

// 函数作用：处理 ls 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 返回值：无。
void handle_ls(int listen_fd,char *current_path);

// 函数作用：处理 pwd 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 返回值：无。
void handle_pwd(int listen_fd,char *current_path);

// 函数作用：处理 rm 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：要删除的文件名。
// 返回值：无。
void handle_rm(int listen_fd,char *current_path,char *arg);

// 函数作用：处理 mkdir 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：要创建的目录名。
// 返回值：无。
void handle_mkdir(int listen_fd,char *current_path,char *arg);

// 函数作用：处理 gets 命令，也就是下载文件。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：要下载的文件名。
// 返回值：无。
void handle_gets(int listen_fd, char *current_path, char *arg);

// 函数作用：处理 puts 命令，也就是上传文件。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：要上传后的目标文件名。
// 返回值：无。
void handle_puts(int listen_fd, char *current_path, char *arg);

#endif /* _\=filename_H_ */

