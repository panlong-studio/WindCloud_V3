#include <stdio.h>
#include <string.h>
#include "sha256_utils.h"
#include "log.h"

int get_file_sha256(const char *file_path, char *sha256_out) {
    
    char command[512]={0};

    snprintf(command,sizeof(command),"sha256sum %s",file_path);
    
    FILE *pipe = popen(command, "r");
    if(pipe == NULL) {
        LOG_ERROR("执行 sha256sum 命令获取文件 %s 的 SHA256 值失败", file_path);
        return -1; 
    }

    char buf[1024]={0};

    if(fgets(buf, sizeof(buf), pipe) != NULL) {
       strncpy(sha256_out, buf, 64);
       sha256_out[64] = '\0';
    }else{
        LOG_ERROR("读取 sha256sum 命令输出 文件 %s 的 SHA256 值失败", file_path);
        pclose(pipe);
        return -1; 
    }

    pclose(pipe);
    return 0; 
}
