#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <readline/readline.h>

#include "cpu.h"
#include "memory.h"
#include "expr.h"
#include "watchpoint.h"

struct Command{
    const char* name;
    const char* form;
    const char* desc;
    void (*handler)(const char* args);
};

static void cmd_si(const char* args);
static void cmd_info(const char* args);
static void cmd_x(const char* args);
static void cmd_help(const char* args);
static void cmd_p(const char* args);
static void cmd_w(const char* args);
static void cmd_d(const char* args);

//Command table
static Command cmd_table[] ={
    {"si"  , "si N"                , "Single step execute N instructions (default 1)."        , cmd_si},
    {"info", "info r/<reg_name>/w" , "Show the information of register or the watchpoint pool.", cmd_info},
    {"x"   , "x N ADDR"            , "Examine memory."                                         , cmd_x},
    {"help", "help"                , "Show this help message."                                 , cmd_help},
    {"p"   , "p EXPR"              , "compute the expression and display."                     , cmd_p},
    {"w"   , "w EXPR"              , "set watchpoint."                                         , cmd_w},
    {"d"   , "d N"                 , "delete the watchpoint id as N."                          , cmd_d},
};
static size_t cmd_table_size = sizeof(cmd_table) / sizeof(cmd_table[0]);

// ============================================================
// function realization
// ============================================================

//single step run n commands
static void cmd_si(const char* args)
{
    int n = 1;
    if(args && args[0] !='\0')
    {
        n = atoi(args);
        if(n < 1) n=1;
    }
    printf("(SDB stub) Single step %d instruction(s) ...\n", n);
    //TODO PA2: 执行n条指令
}

//get register information
static void cmd_info(const char* args)
{
    if(!args || args[0] == '\0')
    {
        printf("[ERROR] Usage: info r          - show all registers\n");
        printf("               info <reg_name> - show a single register\n");
        printf("               info w - show the watchpoint pool\n");
        return;
    }

    if(strcmp(args, "r") == 0)
    {
        for(int i=0; i<32; i++)
        {
            printf("%-4s: 0x%08x  ", cpu.get_reg_name(i), cpu.gpr[i]);
            if(i%4 == 3) printf("\n");
        }

        printf("pc  : 0x%08x\n", cpu.pc);
        return;
    }
    else if(strcmp(args, "w") == 0)
    {
        wp_print_all();
        return;
    }

    for(int i=0; i<32; i++)
    {
        if (strcmp(args, cpu.get_reg_name(i) ) == 0)
        {
            printf("%-4s: 0x%08x", cpu.get_reg_name(i), cpu.gpr[i]);
            return;
        }
    }

    if(strcmp(args, "pc") == 0)
    {
        printf("pc  : 0x%08x\n", cpu.pc);
        return;
    }

    printf("Unknown register: '%s'. Use 'info r' to list all registers.\n", args);
}

//    cmd_x(args)    → 查看内存（从 args 解析出 N 和 ADDR）
//查看内存（从 args 解析出 N 和 ADDR）
static void cmd_x(const char* args)
{
    int n = 0;
    uint32_t addr = 0;

    if(sscanf(args, "%d %x", &n, &addr) != 2)
    {
        printf("[ERROR] Usage: x N ADDR (e.g., x 10 0x80000000)\n");
        return;
    }

    if(n<0)
    {
        printf("Invalid count: %d\n", n);
        return;
    }

    printf("(SDB stub) Dump %d words from 0x%08x:\n", n, addr);
    for(int i=0; i<n; i++)
    {
        // TODO PA2: 替换为真实的 pmem_read 内存访问
        // uint32_t val = pmem_read(addr + i*4, 4);
        // printf("  0x%08x: 0x%08x\n", addr + i * 4, val);
    }
}

// Get information of commands
static void cmd_help(const char* args)
{
    (void)args;

    printf("Stratum Debugger (SDB) commands:\n");
    for(size_t i=0;i < cmd_table_size; i++)
    {
        printf("  %-5s - %-20s - %s\n", cmd_table[i].name, cmd_table[i].form, cmd_table[i].desc);
    }
}

static void cmd_p(const char* args)
{
    if(!args || args[0] == '\0')
    {
        printf("[ERROR] Usage: p EXPR\n");
        return;
    }
    bool ok = false;
    uint32_t result = expr_eval(args,&ok);
    if(ok)
    {
        printf("= 0x%08x  (%u)\n",result,result);
    }
}

static void cmd_w(const char* args)
{
    if(!args || args[0] == '\0')
    {
        printf("[ERROR] Usage: w EXPR\n");
        return;
    }
    int id = wp_add(args);
    if(id > 0) printf("Watchpoint %d: [%s] created.\n", id, args);
    else printf("Failed to create watchpoint.\n");
}

static void cmd_d(const char* args)
{
    if(!args || args[0] == '\0')
    {
        printf("[ERROR] Usage: d N\n");
        return;
    }
    int id = atoi(args);
    bool ok = wp_delete(id);
    if(ok)
    {
        printf("Watchpoint %d Delete succeed.\n",id);
    }
    else
    {
        printf("Can't find watch point ID:%d.\n",id);
    }
}

// 4. 实现主循环 sdb_mainloop()
static Command* find_command(const char* name)
{
    for(size_t i = 0; i < cmd_table_size; i++)
    {
        if(strcmp(cmd_table[i].name, name) == 0)
        {
            return &cmd_table[i];
        }
    }
    return nullptr;
}

void sdb_mainloop()
{
    while(1)
    {
        char* line = readline("(sdb) ");
        if(!line || strcmp(line, "q") == 0 )
        {
            printf("Exiting SDB.\n");
            break;
        }
        if(line[0] == '\0')
        {
            free(line);
            continue;
        }

        char* saveptr = nullptr;
        char* cmd_name = strtok_r(line, " ", &saveptr);
        char* cmd_args = strtok_r(nullptr, "", &saveptr);

        if(cmd_name)
        {
            Command* cmd = find_command(cmd_name);
            if(cmd)
            {
                cmd->handler(cmd_args ? cmd_args : "");
            }
            else
            {
                printf("Unknown command: '%s'. Type 'help' for list.\n", cmd_name);
            }
        }
        free(line);
    }
}
