#ifndef DAO_VFS_H
#define DAO_VFS_H

// 根据绝对路径查询节点信息。如果找到，将 id 和 type 写入指针，返回 0；未找到返回 -1。
int dao_get_node_by_path(int user_id, const char *path, int *out_id, int *out_type);

// 获取目录下所有文件和文件夹的名字，用空格拼接存入 output_buf
int dao_list_dir(int user_id, int parent_id, char *output_buf);

// 在数据库中创建一个新节点 (type: 0-文件, 1-目录)
int dao_create_node(int user_id, const char *full_path, int parent_id, const char *file_name, int type);

// 在数据库中创建一个“文件节点”，并把它关联到 files 表中的 file_id。
int dao_create_file_node(int user_id, const char *full_path, int parent_id, const char *file_name, int file_id);

// 根据绝对路径查普通文件节点，并取出 paths 表里的 file_id。
int dao_get_file_info_by_path(int user_id, const char *path, int *out_node_id, int *out_file_id);

// 检查目录是否为空（是否还有子节点），为空返回 1，非空返回 0
int dao_is_dir_empty(int user_id, int dir_id);

// 删除一个节点
int dao_delete_node(int user_id, int node_id);

#endif
