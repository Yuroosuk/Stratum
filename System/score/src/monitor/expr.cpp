#include"expr.h"
#include"cpu.h"
#include<cstring>
#include<regex.h>
#include<cstdio>
#include<cstdint>
#include<cstdlib>

enum TokenType {
    TK_NOTYPE = 0,  // 空白（匹配后忽略，不加入 token 序列）
    TK_NUM,         // 十进制整数，如 123
    TK_HEX,         // 十六进制整数，如 0xff
    TK_REG,         // 寄存器名，如 $ra $a0 $pc
    TK_PLUS,        // +
    TK_MINUS,       // -
    TK_MUL,         // *（同时也用作一元解引用，在 parse 阶段区分）
    TK_DIV,         // /
    TK_LPAREN,      // (
    TK_RPAREN,      // )
    TK_EQ,          // ==
    TK_NEQ,         // !=
    TK_AND,         // &&
    TK_DEREF,       // 一元 *（在 mark_deref 中标记，不是词法阶段的类型）
};

struct Rule {
    const char *pattern;
    TokenType   type;
};

static Rule rules[] = {
    {" +",               TK_NOTYPE},  // 空白
    {"0[xX][0-9a-fA-F]+", TK_HEX},  // 0x... 必须在 TK_NUM 之前！
    {"[0-9]+",           TK_NUM},
    {"\\$[a-z][a-z0-9]*", TK_REG},
    {"==",               TK_EQ},     // == 必须在单字符之前
    {"!=",               TK_NEQ},
    {"&&",               TK_AND},
    {"\\+",              TK_PLUS},
    {"-",                TK_MINUS},
    {"\\*",              TK_MUL},
    {"/",                TK_DIV},
    {"\\(",              TK_LPAREN},
    {"\\)",              TK_RPAREN},
};

struct Token {
    TokenType type;
    char str[64];
};

#define NR_RULES ((int)(sizeof(rules) / sizeof(rules[0])))

static Token tokens[256];  // 词法分析结果存放处
static int   nr_token;     // 当前 token 数量
static regex_t re[NR_RULES];
static bool re_inited = false;

uint32_t parse_expr(int* pos, bool* ok);

static bool init_regex()
{
    for(int i=0; i<NR_RULES; i++)
    {
        int ret = regcomp(&re[i],rules[i].pattern, REG_EXTENDED);
        if(ret != 0)
        {
            char errbuf[128];
            regerror(ret,&re[i], errbuf, sizeof(errbuf));
            fprintf(stderr, "正则编译失败 [%s]: %s\n",rules[i].pattern, errbuf);
            return false;
        }
    }
    return true;
}

//将输入切分为token
static bool tokenize(const char *e) {
    // 初始化正则（只编译一次，用 static bool 控制）
    if(!re_inited)
    {
        if(!init_regex()) return false;
        re_inited = true;
    }

    nr_token = 0;
    int pos = 0;
    int len = (int)strlen(e);

    while (pos < len) {
        // 逐条尝试规则
        for (int i = 0; i < NR_RULES; i++) {
            regmatch_t pmatch;
            if (regexec(&re[i], e + pos, 1, &pmatch, 0) == 0
                && pmatch.rm_so == 0) {
                // 在 pos 位置匹配成功，匹配长度为 pmatch.rm_eo
                int mlen = pmatch.rm_eo;
                if (rules[i].type == TK_NOTYPE) {
                    pos += mlen;  // 空白直接跳过
                    goto next;
                }
                // 记录 Token：复制原始字符串，设置类型
                if(nr_token>=256)
                {
                    fprintf(stderr, "词法错误:Token 数量超过上限 256\n");
                    return false;
                }
                tokens[nr_token].type = rules[i].type;
                if (mlen >= 64) mlen = 63;
                memcpy(tokens[nr_token].str, e+pos,mlen);
                tokens[nr_token++].str[mlen] = '\0';
                pos += mlen;
                goto next;
            }
        }
        // 没有规则匹配：词法错误
        fprintf(stderr, "词法错误：无法识别字符 '%c' 位于位置 %d\n", e[pos], pos);
        return false;
    next:;
    }
    return true;
}

static void mark_deref() {
    for (int i = 0; i < nr_token; i++) {
        if (tokens[i].type == TK_MUL) {
            if (i == 0
                || (tokens[i-1].type != TK_RPAREN
                 && tokens[i-1].type != TK_NUM
                 && tokens[i-1].type != TK_HEX
                 && tokens[i-1].type != TK_REG)) {
                tokens[i].type = TK_DEREF;
            }
        }
    }
}

uint32_t parse_primary(int* pos, bool* ok)
{
    if(*pos >= nr_token)
    {
        fprintf(stderr, "[ERROR] parse_expr : exprssion is not complete.\n");
        *ok = false;
        return 0;
    }
    if( *pos < nr_token && (tokens[*pos].type == TK_NUM || tokens[*pos].type == TK_HEX || tokens[*pos].type == TK_REG ||
                            tokens[*pos].type == TK_LPAREN) )                             
    {
        uint32_t val = 0;
        switch(tokens[*pos].type)
        {
            case TK_NUM:
                return (uint32_t)strtoul(tokens[(*pos)++].str,nullptr,10);
            case TK_HEX:
                return (uint32_t)strtoul(tokens[(*pos)++].str+2,nullptr,16);
            case TK_REG:
                if(strcmp(tokens[*pos].str+1,"pc")==0)
                {
                    (*pos)++;
                    return cpu.pc;
                }
                for(int i=0;i<32;i++)
                {
                    if(strcmp(tokens[*pos].str+1,GPR_NAMES[i])==0)
                    {
                        (*pos)++;
                        return cpu.gpr[i];
                    }
                }
                *ok = false;
                fprintf(stderr, "[ERROR] parse_expr : can't find the register [%s]\n.",tokens[*pos].str);
                return 0;
            case TK_LPAREN:
                (*pos)++;
                val = parse_expr(pos, ok);
                if(!(*ok))
                {
                    return 0;
                }
                if(*pos >= nr_token || tokens[*pos].type != TK_RPAREN)
                {
                    fprintf(stderr, "[ERROR] parse_expr : lacks the right rapen.\n");
                    *ok = false;
                    return 0;
                }
                (*pos)++;
                return val;
            default: break;
        }
    }
    fprintf(stderr, "[ERROR] parse_expr : no primary [%s]\n",tokens[*pos].str);
    *ok = false;
    return 0;
}

uint32_t parse_unary(int* pos, bool* ok)
{
    if(*pos < nr_token && (tokens[*pos].type == TK_MINUS || tokens[*pos].type == TK_DEREF) )
    {
        TokenType op = tokens[(*pos)++].type;
        if(*pos >= nr_token)
        {
            *ok = false;
            return 0;
        }
        uint32_t rhs = parse_unary(pos,ok);
        if(!(*ok))
        {
            return 0;
        }

        switch(op)
        {
            case TK_MINUS:
                return -rhs;
            case TK_DEREF:
                //TO DO:paddr_read
                return 0;
            default: break;
        }
    }
    if(*pos >= nr_token)
    {
        *ok = false;
        return 0;
    }
    return parse_primary(pos,ok);
}

uint32_t parse_mul(int* pos, bool* ok)
{
    uint32_t val = parse_unary(pos,ok);
    while(*pos < nr_token && (tokens[*pos].type == TK_MUL || tokens[*pos].type == TK_DIV) )
    {
        TokenType op = tokens[(*pos)++].type;
        if(*pos >= nr_token)
        {
            *ok = false;
            return 0;
        }
        uint32_t rhs = parse_unary(pos,ok);
        if(!(*ok))
        {
            return 0;
        }

        switch(op)
        {
            case TK_MUL:
                val = val * rhs;
                break;
            case TK_DIV:
                if(rhs == 0)
                {
                    *ok = false;
                    fprintf(stderr, "[ERROR] parse_expr : divide zero.\n");
                    return 0;
                }
                val = val / rhs;
                break;
            default: break;
        }
    }
    return val;
}

uint32_t parse_add(int* pos, bool* ok)
{
    uint32_t val = parse_mul(pos,ok);
    while(*pos < nr_token && (tokens[*pos].type == TK_PLUS || tokens[*pos].type == TK_MINUS) )
    {
        TokenType op = tokens[(*pos)++].type;
        if(*pos >= nr_token)
        {
            *ok = false;
            return 0;
        }
        uint32_t rhs = parse_mul(pos,ok);
        if(!(*ok))
        {
            return 0;
        }

        switch(op)
        {
            case TK_PLUS:
                val = val + rhs;
                break;
            case TK_MINUS:
                val = val - rhs;
                break;
            default: break;
        }
    }
    return val;
}

uint32_t parse_eq(int* pos, bool* ok)
{
    uint32_t val = parse_add(pos,ok);
    while(*pos < nr_token && (tokens[*pos].type == TK_EQ || tokens[*pos].type == TK_NEQ) )
    {
        TokenType op = tokens[(*pos)++].type;
        if(*pos >= nr_token)
        {
            *ok = false;
            return 0;
        }
        uint32_t rhs = parse_add(pos,ok);
        if(!(*ok))
        {
            return 0;
        }

        switch(op)
        {
            case TK_EQ:
                val = val == rhs;
                break;
            case TK_NEQ:
                val = val != rhs;
                break;
            default: break;
        }
    }
    return val;
}

uint32_t parse_expr(int* pos, bool* ok)
{
    uint32_t val = parse_eq(pos,ok);
    while(*pos < nr_token && tokens[*pos].type == TK_AND)
    {
        (*pos)++;
        if(*pos >= nr_token)
        {
            *ok = false;
            return 0;
        }
        uint32_t rhs = parse_eq(pos,ok);
        if(!(*ok))
        {
            return 0;
        }

        val = val && rhs;
    }
    return val;
}


uint32_t expr_eval(const char *e, bool *success) {
    *success = false;
    if (!tokenize(e)) return 0;
    mark_deref();

    int  pos = 0;
    bool ok  = true;
    uint32_t val = parse_expr(&pos, &ok);

    if (!ok || pos != nr_token) {
        if(ok) fprintf(stderr, "表达式未完全解析\n");
        return 0;
    }
    *success = true;
    return val;
}