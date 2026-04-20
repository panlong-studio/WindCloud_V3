#ifndef _MD5_UTILS_H_
#define _MD5_UTILS_H_

//函数作用：通过popen调用系统命令计算文件的MD5值，并将结果输出到md5_out中
//参数说明：
//filepath：要计算MD5值的文件路径
//md5_out：用于存储计算结果的字符数组，必须至少有33个字符的空间
//        （32个字符用于MD5值，1个字符用于字符串结束符'\0'）
//返回值：成功返回0，失败返回-1

int get_file_md5(const char *file_path, char *md5_out);

#endif
