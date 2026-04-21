#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dao_vfs.h"
#include "db_pool.h"
#include "log.h"

//根据绝对路径查询节点信息
//如果找到 将id和type写入指针 返回 0
//如果找不到 返回 -1
int dao_get_node_by_path(int user_id,const char *path,int *out_id,int *out_type){
    if(strcmp(path,"/")==0){
        *out_id=0;//根目录的 id 是 0，type 是 1
        *out_type=1;
        return 0;
    }

    char sql[512];
    snprintf(sql,sizeof(sql),"SELECT id,type FROM paths WHERE user_id=%d AND path='%s'",user_id,path);

    MYSQL_RES *res=db_execute_query(sql);
    if(res==NULL){
        LOG_ERROR("数据库查询失败: %s",sql);
        return -1;
    }

    MYSQL_ROW row=mysql_fetch_row(res);
    if(row!=NULL){
        *out_id=atoi(row[0]);
        *out_type=atoi(row[1]);
        mysql_free_result(res);
        return 0;
    }
    mysql_free_result(res);
    return -1;
}

//获取目录下所有文件和文件夹的名字，用空格拼接存入 output_buf 中
//返回值：拼接了多少个字符，0表示空目录，-1表示查询失败
int dao_list_dir(int user_id,int parent_id,char*output_buf){
    char sql[512];
    snprintf(sql,sizeof(sql),"SELECT file_name,type FROM paths WHERE user_id=%d AND parent_id=%d",user_id,parent_id);

    MYSQL_RES *res=db_execute_query(sql);
    if(res==NULL){
        LOG_ERROR("数据库查询失败: %s",sql);
        return -1;
    }

    int used=0;
    MYSQL_ROW row;
    while((row=mysql_fetch_row(res))!=NULL){
        const char* name=row[0];
        int type=atoi(row[1]);

        if(type==1){
            used+=sprintf(output_buf+used,"\033[34m%s\033[0m ",name);//目录用蓝色显示
        } else {
            used+=sprintf(output_buf+used,"%s ",name);
        }
    }
    mysql_free_result(res);
    return used;//返回拼接了多少个字符，0表示空目录
}

//在数据库中创建一个新节点（type=0 是文件，type=1 是目录）
//返回值：成功返回 0，失败返回 -1
int dao_create_node(int user_id,const char*full_path,
                    int parent_id,const char*file_name,int type){
    char sql[1024];
    //这里的 file_id 传 NULL，因为还没有关联文件实体
    snprintf(sql,sizeof(sql),"INSERT INTO paths (user_id,path,parent_id,file_name,type) VALUES (%d,'%s',%d,'%s',%d)",
             user_id,full_path,parent_id,file_name,type);
    return db_execute_update(sql);
}

//检查目录是否为空
//返回值：1 表示空，0 表示非空，-1 表示查询失败
int dao_is_dir_empty(int user_id,int dir_id){
    char sql[256];
    snprintf(sql,sizeof(sql),"SELECT id FROM paths WHERE user_id=%d AND parent_id=%d LIMIT 1",user_id,dir_id);
    MYSQL_RES *res=db_execute_query(sql);
    if(res==NULL){
        LOG_ERROR("检查目录是否为空时数据库查询失败: %s",sql);
        return -1;//查询失败
    }

    MYSQL_ROW row=mysql_fetch_row(res);
    int empty=(row==NULL)?1:0;
    mysql_free_result(res);
    return empty;
}

//删除一个节点
//返回值：成功返回 0，失败返回 -1
int dao_delete_node(int user_id,int node_id){
    char sql[256];
    snprintf(sql,sizeof(sql),"DELETE FROM paths WHERE id=%d AND user_id=%d",node_id,user_id);
    return db_execute_update(sql);
}