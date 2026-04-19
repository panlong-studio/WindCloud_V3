#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include "queue.h"
#include "thread_pool.h"
#include "worker.h"
#include "epoll.h"
#include "server_socket.h"
#include "config.h"
#include "handle.h"
#include "error_check.h"


// pipe_fd[0] 用来读，pipe_fd[1] 用来写。
// 父进程收到 Ctrl+C 后，会往管道里写一个字节。
// 子进程的 epoll 监听到这个字节后，就进入退出流程。
int pipe_fd[2];

// 函数作用：SIGINT 的信号处理函数。
// 参数 num：信号编号，这里通常是 SIGINT。
// 返回值：无。
void func(int num){
    // 打印收到的信号编号，方便调试。
    printf("num=%d\n",num);

    // 往管道里写一个字节，通知子进程该退出了。
    write(pipe_fd[1],"1",1);
}

// 函数作用：从配置文件中读取服务端要监听的 IP 和端口。
// 参数 ip：输出参数，用来保存 IP。
// 参数 port：输出参数，用来保存端口。
// 返回值：无。
void load_config(char *ip, char *port) {
    // 读取 ip。
    get_target("ip", ip);
    printf("ip=%s\n", ip);

    // 读取 port。
    get_target("port", port);
    printf("port=%s\n", port);
}

// 函数作用：服务端主函数。
// 参数：无。
// 返回值：正常结束返回 0。
int main(){
    // 先初始化日志。
    // 否则 socket/bind/accept 等调用一旦失败，ERROR_CHECK 无法安全打印日志。
    init_log("INFO", NULL);

    // 忽略 SIGPIPE。
    // 这样当对端断开连接后，send 不会直接把进程打死。
    signal(SIGPIPE, SIG_IGN);

    // ip 和 port 用来保存配置文件中的监听地址。
    char ip[64] = {0}; 
    char port[64] = {0};  

    // 加载配置。
    load_config(ip, port);

    // 创建匿名管道，用于父进程通知子进程退出。
    pipe(pipe_fd);
    
    // fork 之后会分成父子两个进程。
    // 父进程专门负责监听 Ctrl+C。
    // 子进程负责真正跑服务器。
    if(fork() != 0){
        // 父进程收到 SIGINT 后，就执行上面的 func。
        signal(SIGINT, func);

        // 父进程等待子进程结束。
        wait(NULL);
        exit(0);
    }

    // 子进程把自己放进新的进程组，避免和父进程完全绑死在一起。
    setpgid(0, 0);

    // listen_fd 是服务端监听新连接用的 socket。
    int listen_fd = 0;
    init_socket(&listen_fd, ip, port);

    // 创建线程池。
    thread_pool_t pool;
    init_thread_pool(&pool, 5);

    // 创建 epoll 实例。
    int epfd = epoll_create(1);
    ERROR_CHECK(epfd, -1, "epoll_create");

    // 监听 listen_fd：表示有新客户端到来。
    add_epoll_fd(epfd, listen_fd);

    // 监听 pipe_fd[0]：表示父进程通知子进程退出。
    add_epoll_fd(epfd, pipe_fd[0]);

    // 主循环，持续等待 epoll 事件。
    while(1){
        // lst 用来保存本轮就绪的事件列表。
        struct epoll_event lst[10];

        // epoll_wait 会阻塞，直到至少有一个 fd 就绪。
        int nready = epoll_wait(epfd, lst, 10, -1);
        ERROR_CHECK(nready, -1, "epoll_wait");
        printf("nready=%d\n", nready);
        
        // 依次处理本轮所有就绪事件。
        for(int idx = 0; idx < nready; idx++){
            // 取出当前就绪的 fd。
            int fd = lst[idx].data.fd;

            if(fd == pipe_fd[0]){
                // 读走管道中的退出通知字节。
                char buf[10];
                read(fd, buf, sizeof(buf));
                printf("子进程收到父进程的终止信号\n");

                // 修改线程池共享数据前，先加锁。
                pthread_mutex_lock(&pool.lock);

                // 置 1 表示线程池进入退出状态。
                pool.exitFlag = 1;

                // 先把队列里还没处理的连接全部清掉。
                // 否则线程被唤醒后，可能还会继续拿旧任务。
                while(pool.queue.size > 0){
                    int client_fd = deQueue(&pool.queue);
                    if(client_fd != -1){
                        shutdown(client_fd, SHUT_RDWR);
                        close(client_fd);
                    }
                }

                // 再把每个工作线程当前正在处理的连接主动 shutdown。
                // 这样阻塞在 recv 的线程更容易尽快返回。
                for(int i = 0; i < pool.num; i++){
                    if(pool.busy_fds[i] != -1){
                        shutdown(pool.busy_fds[i], SHUT_RDWR);
                    }
                }

                // 唤醒所有还睡在条件变量上的线程。
                pthread_cond_broadcast(&pool.cond);
                pthread_mutex_unlock(&pool.lock);

                // 监听 socket 也可以关掉了，因为服务端已经准备退出，不再接新连接。
                close(listen_fd);
                
                // 等待每个工作线程退出。
                for(int i = 0; i < pool.num; i++){
                    pthread_join(pool.thread_id_arr[i], NULL);
                }

                // 回收线程池资源。
                destroy_thread_pool(&pool);

                // 关闭管道两端和 epoll。
                close(pipe_fd[0]);
                close(pipe_fd[1]);
                close(epfd);

                // 最后关闭日志。
                close_log();
                return 0;
            }

            if(fd == listen_fd){
                // 有新客户端到来时，accept 会返回一个新的连接 fd。
                int conn_fd = accept(listen_fd, NULL, NULL);
                ERROR_CHECK(conn_fd, -1, "accept");

                // 把新连接放进线程池任务队列。
                pthread_mutex_lock(&pool.lock);
                enQueue(&pool.queue, conn_fd);

                // 唤醒一个工作线程来处理这个新连接。
                pthread_cond_signal(&pool.cond);
                pthread_mutex_unlock(&pool.lock);
            }
        }
    }

    // 理论上正常不会走到这里，写上只是让资源回收更完整。
    close_log();
    return 0;
}
