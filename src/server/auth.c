#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include <time.h>
#include "auth.h"
#include "session.h"
#include "dao_user.h"
#include "log.h"

//========内部工具=======

//----二进制转十六进制字符串----
//bin:输入二进制数据，hex:输出十六进制字符串，len:二进制数据长度占用字节数
static void bin_to_hex(const unsigned char *bin,char *hex,int len){
    for(int i=0;i<len;i++){
        sprintf(hex+i*2,"%02x",bin[i]);
    }
    hex[len*2]='\0';
}

//----生成随机盐值----
//salt:输出盐值字符串
static void generate_salt(char*salt){
    unsigned char random_bytes[16];
    if(RAND_bytes(random_bytes, sizeof(random_bytes)) != 1){
        srand(time(NULL));
        for(int i=0;i<16;i++){
            random_bytes[i]=rand()%256;
        }
    }
    bin_to_hex(random_bytes, salt, 16);
}

//----计算加盐哈希----
//password:用户输入的密码，salt:用户的盐值，output:输出哈希字符串
static void hash_password_with_salt(const char *password,const char*salt,char*output){
    char salted_password[256];
    unsigned char hash[SHA256_DIGEST_LENGTH];
    snprintf(salted_password, sizeof(salted_password), "%s%s", password, salt);
    SHA256((unsigned char*)salted_password, strlen(salted_password), hash);
    bin_to_hex(hash, output, SHA256_DIGEST_LENGTH);
}

//========业务处理逻辑========
void handle_register(int client_fd,const char*data,int *user_id){
    char *user_name=strtok((char*)data,"/");
    char *user_passwd=strtok(NULL,"\r\n\t");

    if(!user_name || !user_passwd){
        send_msg(client_fd,"错误：用户名或密码格式不正确");
        *user_id=-1;
        return;
    }

    //生成盐值和哈希
    char salt[33];
    char password_hash[65];
    generate_salt(salt);
    hash_password_with_salt(user_passwd, salt, password_hash);

    //插入数据库
    if(dao_insert_user(user_name, password_hash, salt) == 0){
        int new_id;
        char tmp_hash[65], tmp_salt[33];
        if(dao_get_user_by_name(user_name, &new_id, tmp_hash, tmp_salt) == 0){
            *user_id = new_id;
        }
        send_msg(client_fd, "注册成功！请继续登录。");   
    }else{
        *user_id=-1;
        send_msg(client_fd, "注册失败");
    }
}


void handle_login(int client_fd,const char*data,int *user_id){
    char *user_name=strtok((char*)data,"/");
    char *user_passwd=strtok(NULL,"\r\n\t");

    if(!user_name || !user_passwd){
        send_msg(client_fd,"错误：用户名或密码格式不正确");
        *user_id=-1;
        return;
    }

    int db_user_id;
    char stored_hash[65];
    char salt[33];

    //调用DAO查询用户信息
    if(dao_get_user_by_name(user_name,&db_user_id,stored_hash,salt)!=0){
        *user_id=-1;
        send_msg(client_fd,"用户名不存在");
        return;
    }

    //校验密码
    char computed_hash[65];
    hash_password_with_salt(user_passwd, salt, computed_hash);

    if(strcmp(computed_hash,stored_hash)==0){
        *user_id=db_user_id;
        char msg[128];
        snprintf(msg, sizeof(msg), "登录成功！欢迎您，user_id=%d！", *user_id);
        send_msg(client_fd, msg);
    }else{
        *user_id=-1;
        send_msg(client_fd,"密码错误");
    } 
}