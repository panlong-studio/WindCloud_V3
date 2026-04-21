#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "db_pool.h"
#include "log.h"

#define MAX_POOL_SIZE 10

//======连接池内部结构（对外隐藏）=======
static MYSQL* conn_pool[MAX_POOL_SIZE];
static int pool_top = -1; //栈顶指针 -1 表示空池
static int g_pool_size = 0;

static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pool_cond = PTHREAD_COND_INITIALIZER;

//----连接池内部辅助函数（获取一个空连接）----
static MYSQL* get_connection(){
    pthread_mutex_lock(&pool_lock);

    //如果连接池空了，等待其他线程归还连接
    while(pool_top==-1){
        pthread_cond_wait(&pool_cond,&pool_lock);
    }
    MYSQL* conn=conn_pool[pool_top];
    pool_top--;//出栈

    pthread_mutex_unlock(&pool_lock);
    return conn;
}

//----连接池内部辅助函数（归还一个连接）----
static void release_connection(MYSQL *conn){
    if(conn==NULL) return;

    pthread_mutex_lock(&pool_lock);

    pool_top++;
    conn_pool[pool_top]=conn;//入栈

    pthread_cond_signal(&pool_cond);//通知等待的线程有连接可用
    pthread_mutex_unlock(&pool_lock);
}
//===================================


//======对外接口实现========

//初始化数据库连接池
int init_db_pool(const char* host,const char*user,const char*pwd,
                 const char*db_name,int pool_size){
    //限制连接池大小，防止过大导致资源耗尽
    if(pool_size>MAX_POOL_SIZE){
        pool_size=MAX_POOL_SIZE;
    }

    g_pool_size=pool_size;

    for(int i=0;i<pool_size;i++){
        MYSQL *conn=mysql_init(NULL);
        if(conn==NULL){
            LOG_ERROR("db_pool:MySQL 句柄初始化失败");
            return -1;
    }

    //开启自动重连机制 （防止服务端挂机一夜 mysql 连接断开）
    //char reconnect=1;
    //mysql_options(conn,MYSQL_OPT_RECONNECT,&reconnect);

    //连接数据库
    if(mysql_real_connect(conn,host,user,pwd,db_name,0,NULL,0)==NULL){
        LOG_ERROR("db_pool:MySQL 连接失败: %s",mysql_error(conn));
        mysql_close(conn);
        return -1;
    }
    pool_top++;
    conn_pool[pool_top]=conn;
}

LOG_INFO("db_pool:数据库连接池初始化成功，池大小=%d",pool_size);
return 0;
}

//销毁连接池（服务端退出时调用）
void destroy_db_pool(){
    pthread_mutex_lock(&pool_lock);
    for(int i=0;i<=pool_top;i++){
        mysql_close(conn_pool[i]);
    }
    pool_top=-1;
    pthread_mutex_unlock(&pool_lock);
    LOG_INFO("db_pool:数据库连接池销毁成功");
}

//执行增删改（insert, update, delete）
//返回：0 成功，-1 失败
int db_execute_update(const char* sql){
    MYSQL *conn=get_connection();
    int ret=mysql_query(conn,sql);
    if(ret!=0){
        LOG_ERROR("db_pool:执行 SQL 失败: %s, SQL: %s",mysql_error(conn),sql);
        release_connection(conn);
        return -1;
    }
    release_connection(conn);
    return 0;
}

//执行查询（select）
//返回：查询结果集指针，失败返回 NULL
MYSQL_RES *db_execute_query(const char* sql){
    MYSQL *conn=get_connection();
    if(mysql_query(conn,sql)!=0){
        LOG_ERROR("db_pool:执行 SQL 失败: %s, SQL: %s",mysql_error(conn),sql);
        release_connection(conn);
        return NULL;
    }

    MYSQL_RES *res=mysql_store_result(conn);

    release_connection(conn);

    return res;
}
