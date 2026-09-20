#pragma once
#include <cstdint>

int  wp_add(const char *expr_str);   // 返回 watchpoint 编号，失败返回 -1
bool wp_delete(int id);               // 删除指定编号，返回是否成功
void wp_print_all();                  // info w 命令使用
bool wp_check();                      // 返回是否有 watchpoint 触发