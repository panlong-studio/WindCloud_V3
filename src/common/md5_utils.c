#include <stdio.h>
#include <string.h>
#include "md5_utils.h"
#include "log.h"

int get_file_md5(const char *file_path, char *md5_out) {
    
    char command[512]={0};

    snprintf(command,sizeof(command),"md5sum %s",file_path);
    
    FILE *pipe = popen(command, "r");
    if(pipe == NULL) {
        LOG_ERROR("执行 md5sum 命令获取文件 %s 的 MD5 值失败", file_path);
        return -1; 
    }

    char buf[1024]={0};

    if(fgets(buf, sizeof(buf), pipe) != NULL) {
       strncpy(md5_out, buf, 32);
       md5_out[32] = '\0';
    }else{
        LOG_ERROR("读取 md5sum 命令输出 文件 %s 的 MD5 值失败", file_path);
        pclose(pipe);
        return -1; 
    }

    pclose(pipe);
    return 0; 
}
