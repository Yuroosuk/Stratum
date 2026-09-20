#include<cstdint>
#include<cstdio>
#include<cstring>
#include"watchpoint.h"
#include"expr.h"

struct Watchpoint {
    int      id;
    char     expr[128];
    uint32_t last_val;
    bool     in_use;
};

static Watchpoint wp_pool[32];
static int        next_id = 1;

int wp_add(const char *expr_str)
{
    for(int i=0; i<32;i++)
    {
        if(wp_pool[i].in_use == false)
        {
            if(strlen(expr_str) >= (int)sizeof(wp_pool[0].expr))
            {
                fprintf(stderr, "[ERROR] wp_add : expression exceeds the capacity[%d].\n", (int)sizeof(wp_pool[0].expr)-1);
                return -1;
            }
            bool success = false;
            wp_pool[i].last_val = expr_eval(expr_str, &success);
            if(!success)
            {
                fprintf(stderr, "[ERROR] wp_add : expression evaluation failed [%s].\n", expr_str);
                return -1;
            }
            strcpy(wp_pool[i].expr, expr_str);
            wp_pool[i].id = next_id++;
            wp_pool[i].in_use = true;
            return wp_pool[i].id;
        }
    }
    fprintf(stderr, "[ERROR] wp_add : watchpoint is full.\n");
    return -1;
}

bool wp_delete(int id)
{
    for(int i=0; i<32;i++)
    {
        if(wp_pool[i].in_use == true && wp_pool[i].id == id)
        {
            wp_pool[i].in_use = false;
            return true;
        }
    }
    return false;
}

void wp_print_all()
{
    printf("========================================\n");
    printf("  Print the watchpoint pool\n");
    printf("========================================\n\n");
    bool emp = true;
    for(int i=0; i<32;i++)
    {
        if(wp_pool[i].in_use == true)
        {
            emp = false;
            printf("[%d] %-16s : %u\n",wp_pool[i].id,wp_pool[i].expr,wp_pool[i].last_val);
        }
    }
    printf("\n");
    if(emp)
    {
        printf("The watchpoint pool is empty.\n\n");
    }
}

bool wp_check()
{
    bool triggered = false;
    for(int i=0; i<32;i++)
    {
        if(wp_pool[i].in_use == true)
        {
            bool success = false;
            uint32_t new_val = expr_eval(wp_pool[i].expr, &success);
            if(!success)
            {
                fprintf(stderr, "[ERROR] wp_check : watchpoint[%d] expression evaluation failed: %s.\n", wp_pool[i].id, wp_pool[i].expr);
                continue;
            }
            if(wp_pool[i].last_val != new_val)
            {
                if(!triggered)
                {
                    printf("========================================\n");
                    printf("  Check the watchpoint pool\n");
                    printf("========================================\n\n");
                    triggered = true;
                }
                printf("[%d] %-16s , (last) %u : (new) %u\n",wp_pool[i].id,wp_pool[i].expr,wp_pool[i].last_val,new_val);
                wp_pool[i].last_val = new_val;
            }
        }
    }
    return triggered;
}