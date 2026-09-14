#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <readline/readline.h>

#include "cpu.h"
#include "memory.h"

struct Command{
    const char* name;
    const char* desc;
    void (*handler)(const char* args);
};

static void cmd_si(const char* args);
static void cmd_info(const char* args);
static void cmd_x(const char* args);
static void cmd_help(const char* args);

//Command table
static Command cmd_table[] ={
    {"si"  , "Single step execute N instructions (default 1)", cmd_si},
    {"info", "Show register information"                     , cmd_info},
    {"x"   , "Examine memory"                                , cmd_x},
    {"help", "Show this help message"                        , cmd_help},
};
static size_t cmd_table_size = sizeof(cmd_table) / sizeof(cmd_table[0]);

// ============================================================
// function realization
// ============================================================

//single step run n commands
static void cmd_si(const char* args)
{
    int n;
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
        printf("Usage: info r          - show all registers\n");
        printf("       info <reg_name> - show a single register\n");
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
void cmd_x(const char* args)
{
    int n = 0;
    uint32_t addr = 0;

    if(sscanf(args, "%d %x", &n, &addr) != 2)
    {
        printf("Usage: x N ADDR (e.g., x 10 0x80000000)\n");
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
void cmd_help(const char* args)
{
    (void)args;

    printf("Stratum Debugger (SDB) commands:\n");

    extern Command cmd_table[];
    extern size_t cmd_table_size;

    for(size_t i=0;i < cmd_table_size; i++)
    {
        printf("  %-8s - %s\n", cmd_table[i].name, cmd_table[i].desc);
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
