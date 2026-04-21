#ifndef _DAO_VFS_H_
#define _DAO_VFS_H_

//根据绝对路径查询节点信息
//如果找到 将id和type写入指针 返回 0
//如果找不到 返回 -1
int dao_get_node_by_path(int user_id,const char *path,int *out_id,int *out_type);

//获取目录下所有文件和文件夹的名字，用空格拼接存入 output_buf 中
//返回值：拼接了多少个字符，0表示空目录，-1表示查询失败
int dao_list_dir(int user_id,int parent_id,char*output_buf);

//在数据库中创建一个新节点（type=0 是文件，type=1 是目录）
int dao_create_node(int user_id,const char*full_path,
                    int parent_id,const char*file_name,int type);

//检查目录是否为空 返回 1 是空目录 0 不是空目录 -1 查询失败
int dao_is_dir_empty(int user_id,int dir_id);

//删除一个节点
//返回值：0 成功 -1 失败
int dao_delete_node(int user_id,int node_id);

#endif // _DAO_VFS_H_
