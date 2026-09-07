# 实验手册 · PA1-B：表达式解析器

> **本手册使用者：成员 B**  
> **阶段目标：** 实现 `expr_eval()` 函数，能对包含四则运算、括号、寄存器引用、内存解引用的表达式求值；实现 watchpoint 的添加、删除、检查功能。  
> **预计时间：** 2～3 天  
> **前置条件：** PA0 全部完成；成员 A 已提交 `cpu.h`（包含 `CPU_state` 和 `GPR_NAMES`）  
> **本阶段涉及文件：**
> ```
> score/
> ├── include/
> │   ├── expr.h          ← 本阶段新建
> │   └── watchpoint.h    ← 本阶段新建
> └── src/
>  └── monitor/
>      ├── expr.cpp        ← 本阶段新建
>      ├── watchpoint.cpp  ← 本阶段新建
>      └── test_expr.cpp   ← 本阶段新建（独立测试驱动，联调后删除）
> ```
> **本阶段不涉及：** `sdb.cpp` 的命令循环由成员 A 负责，联调1时你将 `expr.h`/`watchpoint.h` 的接口接入 A 的命令表。  
> **依赖 A 的接口：** `cpu.h`（`CPU_state`、`GPR_NAMES`）、`memory.h`（`paddr_read`）。如果 A 还未提交，先自己写一个最小桩版本用于编译，联调时替换。

---

## 实验一：词法分析（Lexer）

### 目标
把一个表达式字符串切分成 Token（词元）序列。例如：
```
"1+0xff*($ra-2)"  →  [数字:1, +, 十六进制:0xff, *, (, 寄存器:$ra, -, 数字:2, )]
```

### 背景知识

**为什么要分词？**  
直接处理字符串很麻烦（每次都要自己判断"这个字符是数字的开头还是运算符"）。分词之后，每个 Token 都已经识别好了类型，后续的解析只需要处理 Token 序列，代码更清晰。

**用正则表达式匹配 Token：**  
POSIX 正则库（`<regex.h>`）提供 `regcomp` 和 `regexec` 两个函数。工作方式：

1. `regcomp(&re, pattern, REG_EXTENDED)`：编译一个正则表达式
2. `regexec(&re, str, 1, &match, 0)`：在 `str` 中搜索匹配，`match.rm_so == 0` 表示在字符串开头匹配

我们的 tokenizer 工作方式：从字符串头部开始，逐个尝试所有规则，找到在当前位置匹配的那条规则，把对应的字符串片段作为一个 Token，然后移动指针继续。

**规则顺序很重要：**  
`0xff` 和 `0` 都能被"数字"规则匹配，但 `0xff` 应该被识别为十六进制。因此十六进制规则必须排在十进制之前（先匹配长的）。同理，`==` 必须排在 `=` 之前（如果有 `=` 的话）。

### 步骤

**1. 在 `expr.cpp` 中定义 Token 类型枚举**

```cpp
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
```

**2. 定义词法规则表**

```cpp
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
```

**3. 实现 `tokenize` 函数**

```cpp
static Token tokens[256];  // 词法分析结果存放处
static int   nr_token;     // 当前 token 数量

static bool tokenize(const char *e) {
    // 初始化正则（只编译一次，用 static bool 控制）
    // ...

    nr_token = 0;
    int pos = 0;
    int len = strlen(e);

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
                // ...
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
```

> ⚠️ 正则初始化（`regcomp`）只应执行一次。用一个 `static bool inited = false;` 控制，第一次调用时编译所有规则，之后复用。

**4. 对外接口声明（`score/include/expr.h`）**

```cpp
#pragma once
#include <cstdint>

uint32_t expr_eval(const char *e, bool *success);
```

### 检查点
- [] `tokenize("1+0xff")` 产生 3 个 Token：TK_NUM, TK_PLUS, TK_HEX
- [] `tokenize("  1  ")` 空白被忽略，产生 1 个 Token
- [] `tokenize("@")` 返回 false，打印错误信息

---

## 实验二：递归下降解析与求值

### 目标
实现递归下降 Parser，按照运算符优先级计算表达式的值。

### 背景知识

**为什么用递归下降？**  
运算符优先级问题的本质是：`1+2*3` 应该先算乘法再算加法。递归下降把优先级编码进函数调用层次：低优先级的运算符在调用链的外层，高优先级的在内层。

完整的优先级层次（从低到高）：

```
&&          → parse_expr()
== !=       → parse_and()
+ -         → parse_add()     （注意：这里命名可以叫 parse_eq 然后 add）
* /         → parse_mul()
一元 - 一元* → parse_unary()
括号/数字/寄存器 → parse_primary()
```

**每一层的模式是相同的：**

```
parse_add(pos, ok):
    val = parse_mul(pos, ok)        // 先解析更高优先级的
    while 下一个 token 是 + 或 -:
        op = tokens[(*pos)++].type  // 消耗运算符
        rhs = parse_mul(pos, ok)
        val = val + rhs（或 val - rhs）
    return val
```

### 步骤

**1. 标记一元 `*`（解引用）**

扫描 Token 序列，把作为"解引用"用的 `*` 的类型改为 `TK_DEREF`。判断规则：一个 `TK_MUL` 的前一个 Token 不是 `)`、数字或寄存器，则它是一元解引用。

```cpp
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
```

**2. 实现各层 parse 函数**

按照优先级从低到高，依次实现：

- `parse_expr`：处理 `&&`
- `parse_and`：处理 `==` 和 `!=`
- `parse_add`：处理 `+` 和 `-`
- `parse_mul`：处理 `*` 和 `/`（注意除零检查）
- `parse_unary`：处理一元 `-` 和 `TK_DEREF`（`TK_DEREF` 时调用 `paddr_read` 读内存）
- `parse_primary`：处理 `TK_NUM`、`TK_HEX`、`TK_REG`、括号

**`parse_primary` 的实现要点：**

| Token 类型 | 处理方式 |
|------------|---------|
| `TK_NUM` | `strtoul(t .str, nullptr, 10)` |
| `TK_HEX` | `strtoul(t.str + 2, nullptr, 16)`（跳过 `0x` 前缀） |
| `TK_REG` | 若 `t.str+1` 等于 `"pc"` 返回 `cpu.pc`；否则遍历 `GPR_NAMES` 找到对应 `cpu.gpr[i]` |
| `TK_LPAREN` | 递归调用 `parse_expr`，然后期望一个 `TK_RPAREN` |

**3. 实现 `expr_eval`**

```cpp
uint32_t expr_eval(const char *e, bool *success) {
    *success = false;
    if (!tokenize(e)) return 0;
    mark_deref();

    int  pos = 0;
    bool ok  = true;
    uint32_t val = parse_expr(&pos, &ok);

    if (!ok || pos != nr_token) {
        if (ok) fprintf(stderr, "表达式未完全解析\n");
        return 0;
    }
    *success = true;
    return val;
}
```

### 检查点
- [] `expr_eval("1+2*3", &ok)` 返回 7，ok = true
- [] `expr_eval("2*(3+4)", &ok)` 返回 14，ok = true
- [] `expr_eval("0xff", &ok)` 返回 255，ok = true
- [] `expr_eval("1==1", &ok)` 返回 1，ok = true
- [] `expr_eval("1/0", &ok)` 打印除零错误，ok = false，不崩溃
- [] `expr_eval("$xyz", &ok)` 打印未知寄存器，ok = false，不崩溃

---

## 实验三：Watchpoint 模块

### 目标
实现 watchpoint 池：支持最多 32 个 watchpoint，每个记录一个表达式和上次的求值结果；每次 CPU 执行一条指令后检查所有 watchpoint 是否触发。

### 背景知识

Watchpoint 是调试器的核心功能之一：你设置 `w $pc`，每执行一条指令后调试器自动检查 PC 有没有变化，变化了就暂停。这比手动单步要高效得多。

实现思路很简单：
1. 用一个固定大小的数组做"watchpoint 池"（最多 32 个）
2. `wp_add`：找一个空闲槽位，存入表达式字符串，立即对表达式求一次值作为"上次值"
3. `wp_check`：遍历所有在用的 watchpoint，重新求值，与上次值对比，不同则触发

### 步骤

**1. 定义 `score/include/watchpoint.h`**

```cpp
#pragma once
#include <cstdint>

int  wp_add(const char *expr_str);   // 返回 watchpoint 编号，失败返回 -1
bool wp_delete(int id);               // 删除指定编号，返回是否成功
void wp_print_all();                  // info w 命令使用
bool wp_check();                      // 返回是否有 watchpoint 触发
```

**2. 实现 `score/src/monitor/watchpoint.cpp`**

内部数据结构（不需要暴露到头文件）：

```cpp
struct Watchpoint {
    int      id;
    char     expr[128];
    uint32_t last_val;
    bool     in_use;
};

static Watchpoint wp_pool[32];
static int        next_id = 1;
```

各函数实现要点：
- `wp_add`：遍历池找 `in_use == false` 的槽位，初始化字段，用 `expr_eval` 设置 `last_val`
- `wp_delete`：找 `id` 匹配的，把 `in_use` 设为 false
- `wp_print_all`：遍历打印所有 `in_use == true` 的
- `wp_check`：遍历所有在用的，重新求值，不同时打印旧值/新值，更新 `last_val`，返回 true

---

## 实验四：独立测试驱动

### 目标
不依赖成员 A 的 SDB 主循环，用自己写的测试程序驱动验证 `expr_eval` 和 watchpoint。

### 步骤

**1. 编写 `score/src/monitor/test_expr.cpp`**

定义测试用例数组，逐条运行，打印 PASS/FAIL：

```cpp
struct TestCase {
    const char *expr;
    uint32_t    expected;
    bool        should_succeed;  // 预期是否成功求值
};

static TestCase cases[] = {
    {"1",             1,          true},
    {"1+2",           3,          true},
    {"2*3+4",         10,         true},
    {"2*(3+4)",       14,         true},
    {"0xff",          255,        true},
    {"1==1",          1,          true},
    {"1!=1",          0,          true},
    {"1+2==3&&4-1==3", 1,        true},
    {"$zero",         0,          true},
    {"1/0",           0,          false},  // 除零，预期失败
    // 你可以继续添加更多用例...
};
```

**2. 编译并运行**（不依赖 A 的任何代码）

```bash
cd score/src/monitor
g++ -std=c++17 -I../../include \
    expr.cpp watchpoint.cpp \
    ../cpu/cpu.cpp ../memory/memory.cpp \
    test_expr.cpp -o test_expr
./test_expr
```

**3. 确认全部 PASS 后，不要提交 `test_expr.cpp`**

这个文件是临时测试驱动，联调1时删除（`git rm score/src/monitor/test_expr.cpp`）。

### 检查点
- [ ] `test_expr` 编译通过
- [ ] 所有 `should_succeed = true` 的用例返回期望值
- [ ] 所有 `should_succeed = false` 的用例求值失败（ok = false），且不崩溃

---

## 阶段总结

完成本阶段后，你有了：

| 文件 | 内容 |
|------|------|
| `score/include/expr.h` | `expr_eval` 接口（A 的 `p` 命令将使用它） |
| `score/include/watchpoint.h` | watchpoint 接口（A 的 `w`/`d`/`info w` 将使用） |
| `score/src/monitor/expr.cpp` | 完整的词法分析 + 递归下降求值 |
| `score/src/monitor/watchpoint.cpp` | watchpoint 池管理 |

**你现在能回答：**
- 词法分析和语法分析分别做什么？
- 什么是递归下降解析？优先级如何通过调用层次来体现？
- 一元 `*`（解引用）和二元 `*`（乘法）如何区分？
- Watchpoint 的触发机制是什么？

---

## 进入下一步的前提

满足以下所有条件后，等待成员 A 完成 PA1-A，再一起进入「联调1」：

- [ ] 所有检查点通过
- [ ] `test_expr` 所有用例通过
- [ ] 代码已推送到 `feat/pa1-expr-parser` 分支

---

## 常见问题

**Q：`regexec` 总是匹配失败？**  
A：检查 `regcomp` 的返回值是否为 0（0 表示成功）。也要确认规则中的特殊字符有没有正确转义（正则里的 `\*` 在 C 字符串里要写成 `"\\*"`）。

**Q：`parse_primary` 处理寄存器时，`strcmp(t.str + 1, "pc")` 总是不匹配？**  
A：Token 里存的是原始字符串，如 `"$pc"`，所以 `t.str + 1` 是 `"pc"`。确认你的 tokenize 存储方式：是否完整复制了包括 `$` 在内的字符串？

**Q：`(1+2` 这样缺少右括号的表达式程序崩溃了？**  
A：`parse_primary` 遇到 `TK_LPAREN` 后，递归调用 `parse_expr`，然后期望 `TK_RPAREN`。如果 `pos` 已经越界（`pos >= nr_token`），需要设 `*ok = false` 并返回，不能继续访问 `tokens[pos]`。
