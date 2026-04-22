#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dao_user.h"
#include "db_pool.h"
#include "log.h"

//查询用户：成功返回0，并将id、hash、salt填入指针，不存在或失败返回-1
int dao_get_user_by_name(const char*username,int *out_id,char *out_hash,char *out_salt){
    char sql[512];
    snprintf(sql,sizeof(sql),"SELECT id,password_hash,salt FROM users WHERE username='%s'",username);

    //直接调用连接池查询
    MYSQL_RES *res=db_execute_query(sql);
    if(res==NULL){
        LOG_ERROR("查询数据库出错");
        return -1;
    }

    MYSQL_ROW row=mysql_fetch_row(res);
    if(row!=NULL){
        *out_id=atoi(row[0]);
        strcpy(out_hash,row[1]);
        strcpy(out_salt,row[2]);
        mysql_free_result(res);//释放结果集
        return 0;
    }
    mysql_free_result(res);//释放结果集
    return -1;//用户不存在
}


//插入新用户：成功返回0，失败返回-1
int dao_insert_user(const char*username,const char*password_hash,const char*salt){
    char sql[512];
    snprintf(sql,sizeof(sql),"INSERT INTO users(username,password_hash,salt) VALUES('%s','%s','%s')",
             username,password_hash,salt);

    //直接调用连接池更新
    return db_execute_update(sql);
}