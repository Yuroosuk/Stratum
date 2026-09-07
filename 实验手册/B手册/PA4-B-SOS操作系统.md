# 实验手册 · PA4-B：SOS 操作系统

> **本手册使用者：成员 B**  
> **阶段目标：** 实现 SOS（Stratum Operating System）内核：Trap 入口汇编、系统调用处理（write/exit）、简单文件系统（ramdisk）、进程管理（PCB、上下文切换）、Round-Robin 调度，最终能在 SCore 上运行两个用户进程并发执行。  
> **预计时间：** 5～7 天  
> **前置条件：** 联调3 完成（tag `v3.0-pa3`）；成员 A 已提交 `trap.h`（CSR 常量、Trap 原因码）  
> **本阶段涉及文件：**
> ```
> sos/
> ├── Makefile                ← 本阶段新建（内核构建系统）
> ├── include/
> │   ├── proc.h              ← 本阶段新建（PCB 结构体）
> │   └── syscall.h           ← 本阶段新建（系统调用号，与用户程序共享）
> ├── kernel/
> │   ├── trap_entry.S        ← 本阶段新建（Trap 入口汇编）
> │   ├── trap.c              ← 本阶段新建（Trap 分发逻辑）
> │   ├── syscall.c           ← 本阶段新建（系统调用实现）
> │   ├── proc.c              ← 本阶段新建（进程管理）
> │   └── schedule.c          ← 本阶段新建（Round-Robin 调度器）
> ├── fs/
> │   └── ramdisk.c           ← 本阶段新建（内存文件系统）
> └── boot/
>     ├── start.S             ← 本阶段新建（内核启动汇编）
>     └── main.c              ← 本阶段新建（内核主函数）
> ```
> **本阶段不涉及：** `score/` 中的任何代码；内存分页（我们用物理地址，不实现虚拟内存）。  
> **依赖 A 的接口：** `trap.h`（`TRAP_ECALL_M`、`TRAP_TIMER_INT` 等常量）；`score/include/cpu.h` 中 `CPU_state` 的 CSR 字段布局（用于汇编中的 mepc 偏移）。

---

## 实验一：内核启动与 Trap 入口

### 目标
编写内核启动代码和 Trap 入口汇编，让 CPU 发生 ECALL/中断时能正确保存上下文、跳转到 C 处理函数，处理结束后恢复上下文返回。

### 背景知识

**上下文（Context）是什么？**  
当 Trap 打断程序执行时，需要保存当前程序的全部状态（32 个寄存器），以便处理完后完整恢复。这组寄存器值就叫"上下文"。

**为什么用汇编而不是 C？**  
保存上下文必须在任何 C 代码运行之前完成——如果先进入 C 函数，函数本身就会破坏寄存器。汇编能精确控制每一条指令，在进入 C 之前把所有寄存器压栈。

**我们的 Context 结构体（与汇编栈帧对应）：**

```c
// sos/include/proc.h 中定义
typedef struct {
    uint32_t gpr[32];  // x0..x31（汇编按顺序压栈）
    uint32_t pc;       // 保存的 mepc（Trap 发生时的 PC）
    uint32_t cause;    // mcause 的值
} Context;
```

### 步骤

**1. 编写 `sos/boot/start.S`（内核启动汇编）**

```asm
.section .text
.global _start
_start:
    # 设置内核栈（使用 sp 寄存器）
    la   sp, kernel_stack_top

    # 设置 mtvec，指向 Trap 入口
    la   t0, trap_entry
    csrw mtvec, t0

    # 开启机器模式定时器中断（MIE.MTIE = 1）
    li   t0, 0x88      # MIE(bit3) | MTIE(bit7) in mstatus
    csrs mstatus, t0   # 等等，mstatus 和 mie 是不同寄存器！
    # 正确：
    li   t0, 0x08      # mstatus.MIE
    csrs mstatus, t0
    li   t0, 0x80      # mie.MTIE（bit7）
    csrs mie, t0

    # 跳转到 C 代码
    call kernel_main
    # 不应该返回，死循环
1:  j    1b

.section .bss
.align 12
kernel_stack:
    .space 4096   # 4KB 内核栈
kernel_stack_top:
```

**2. 编写 `sos/kernel/trap_entry.S`（Trap 入口汇编）**

这是最关键的汇编代码，理解每一行的含义：

```asm
.global trap_entry
trap_entry:
    # 在栈上分配 Context 空间（32个gpr × 4 + pc × 4 + cause × 4 = 136字节）
    addi sp, sp, -136

    # 保存所有通用寄存器（x0 不需要保存，但占一个位置保持对齐）
    sw   x1,  4(sp)    # ra
    sw   x2,  8(sp)    # 保存 sp 时要保存的是陷入前的 sp（sp + 136）
    # ... 依次保存 x3..x31 ...
    sw   x31, 124(sp)

    # 修正保存的 sp 值（当前 sp 是分配了 Context 后的值，需要恢复原值）
    addi t0, sp, 136
    sw   t0, 8(sp)     # 覆盖 x2（sp）的保存值

    # 保存 mepc 和 mcause
    csrr t0, mepc
    sw   t0, 128(sp)   # ctx->pc = mepc
    csrr t0, mcause
    sw   t0, 132(sp)   # ctx->cause = mcause

    # 以 sp（即 Context * 指针）作为参数调用 C 处理函数
    mv   a0, sp
    call trap_handler

    # 从 Context 恢复（a0 是 trap_handler 返回的 Context 指针，可能是新进程）
    mv   sp, a0

    # 恢复 mepc（从 ctx->pc）
    lw   t0, 128(sp)
    csrw mepc, t0

    # 恢复所有寄存器（跳过 x0）
    lw   x1,  4(sp)
    # ...（跳过 x2/sp，在最后恢复）
    lw   x31, 124(sp)

    # 最后恢复 sp
    lw   sp, 8(sp)

    mret
```

> 关键点：`trap_handler` 接收 `Context *ctx` 作为参数，并**返回**一个 `Context *`（可能是另一个进程的 Context，实现上下文切换）。

**3. 编写 `sos/boot/main.c`**

```c
#include <proc.h>
void proc_init();

void kernel_main() {
    printf("SOS kernel booted!\n");
    proc_init();    // 初始化进程
    // 开始运行第一个进程（进入无限调度循环）
    // ... 见实验四
}
```

### 检查点
- [ ] 用汇编写一个测试：设置 mtvec → 执行 ecall → trap_entry 被调用，`a0` 寄存器正确指向栈上的 Context
- [ ] 读出 Context 中的 `cause` 字段，确认等于 11（ECALL from M-mode）

---

## 实验二：Trap 分发与系统调用

### 目标
在 `trap.c` 中实现 `trap_handler`：根据 `mcause` 区分 ECALL、定时器中断和其他异常，并实现 `write`、`exit` 两个基础系统调用。

### 步骤

**1. 定义 `sos/include/syscall.h`**

系统调用号（与用户程序共享，放入 shal/include 或 sos/include）：

```c
#define SYS_EXIT   1    // exit(status)
#define SYS_WRITE  4    // write(fd, buf, count)
#define SYS_YIELD  11   // 主动让出 CPU（用于触发调度）
// 约定：用户程序将系统调用号放在 a7，参数放在 a0..a2，返回值在 a0
```

**2. 实现 `sos/kernel/trap.c`**

```c
#include "proc.h"

extern Context *schedule(Context *cur);  // 调度器

Context *trap_handler(Context *ctx) {
    uint32_t cause = ctx->cause;

    if (cause == 0x80000007) {
        // 定时器中断：清除 mip.MTIP，触发调度
        // SCore 在 do_trap 里已清除，这里只需调用调度器
        return schedule(ctx);
    }

    if (cause == 11) {
        // ECALL：系统调用
        uint32_t sysno = ctx->gpr[17];  // a7
        ctx->pc += 4;    // 跳过 ecall 指令（否则 mret 会重复执行 ecall）

        syscall_handler(ctx, sysno);
        return ctx;      // 系统调用后继续当前进程（除非 yield/exit）
    }

    // 其他异常：打印并停止
    printf("Unhandled trap! cause=0x%x pc=0x%x\n", cause, ctx->pc);
    while(1);
    return ctx;
}
```

**3. 实现 `sos/kernel/syscall.c`**

```c
void syscall_handler(Context *ctx, uint32_t sysno) {
    switch (sysno) {
    case SYS_WRITE: {
        // a0=fd, a1=buf, a2=count（这里 a0/a1/a2 是 gpr[10/11/12]）
        // fd 忽略（我们只有 stdout），直接用 putch 逐字节输出
        uint32_t buf   = ctx->gpr[11];
        uint32_t count = ctx->gpr[12];
        for (uint32_t i = 0; i < count; i++) {
            putch(*(char *)(buf + i));
        }
        ctx->gpr[10] = count;  // 返回写入字节数（a0）
        break;
    }
    case SYS_EXIT:
        printf("Process exited with code %d\n", ctx->gpr[10]);
        // 标记当前进程为 DEAD，触发调度
        current_proc->state = DEAD;
        // 切换到下一个进程（schedule 会选择下一个 RUNNABLE 进程）
        break;
    case SYS_YIELD:
        // 主动让出 CPU，触发调度（不需要做什么，让 trap_handler 调用 schedule）
        break;
    default:
        printf("Unknown syscall: %d\n", sysno);
        ctx->gpr[10] = -1;
    }
}
```

### 检查点
- [ ] 用户程序执行 `ecall`（SYS_WRITE）后，字符串出现在终端
- [ ] 用户程序执行 `ecall`（SYS_EXIT）后，打印退出信息

---

## 实验三：简单文件系统（Ramdisk）

### 目标
实现一个极简的 Ramdisk：把多个用户程序（ELF 文件）打包进内核，内核启动时能按名字找到对应的 ELF 数据并加载。

### 背景知识

**为什么需要文件系统？**  
内核需要加载用户程序。我们不实现真正的磁盘，而是把用户程序的二进制数据直接嵌入内核：编译内核时，把所有用户程序的字节数组编译进来，运行时就像一个"内存中的文件系统"。

**Ramdisk 的结构：**

```c
// 文件目录项
typedef struct {
    char name[32];
    uint8_t *data;    // 指向嵌入的字节数组
    size_t   size;
} RamdiskEntry;
```

### 步骤

**1. 准备用户程序**

在 `shal/apps/` 下准备两个最简单的用户程序：

- `uprog1`：循环打印 "Program 1 running\n"，每次打印后 `SYS_YIELD`
- `uprog2`：循环打印 "Program 2 running\n"，每次打印后 `SYS_YIELD`

用 `riscv32-unknown-elf-gcc -nostdlib -Ttext 0x80040000` 编译 uprog1（注意：不同程序要用不同的加载地址，否则内存重叠）：
- uprog1 加载地址：`0x80040000`
- uprog2 加载地址：`0x80080000`

**2. 用 `objcopy` 把 ELF 转为 C 字节数组**

```bash
riscv32-unknown-elf-objcopy -O binary uprog1.elf uprog1.bin
xxd -i uprog1.bin > uprog1.c   # 生成 C 数组：unsigned char uprog1_bin[] = {...};
```

**3. 实现 `sos/fs/ramdisk.c`**

```c
#include <string.h>
extern unsigned char uprog1_bin[];
extern unsigned int  uprog1_bin_len;
extern unsigned char uprog2_bin[];
extern unsigned int  uprog2_bin_len;

static RamdiskEntry rd_table[] = {
    {"uprog1", uprog1_bin, 0},  // size 在 ramdisk_init 时设置
    {"uprog2", uprog2_bin, 0},
    {NULL, NULL, 0}
};

void ramdisk_init() {
    rd_table[0].size = uprog1_bin_len;
    rd_table[1].size = uprog2_bin_len;
}

// 按名字查找文件，返回数据指针和大小
uint8_t *ramdisk_get(const char *name, size_t *size) {
    for (int i = 0; rd_table[i].name[0]; i++) {
        if (strcmp(rd_table[i].name, name) == 0) {
            *size = rd_table[i].size;
            return rd_table[i].data;
        }
    }
    return NULL;
}
```

### 检查点
- [ ] `ramdisk_get("uprog1", &sz)` 返回非 NULL，`sz > 0`
- [ ] `sz` 与实际 ELF 文件大小一致

---

## 实验四：进程管理与调度

### 目标
实现 PCB（Process Control Block）、进程创建（从 Ramdisk 加载 ELF 并设置初始上下文）、Round-Robin 调度器。

### 步骤

**1. 定义 `sos/include/proc.h`**

```c
#pragma once
#include <stdint.h>

// 进程状态
typedef enum { RUNNABLE, RUNNING, DEAD } ProcState;

// 进程控制块
typedef struct Proc {
    int       pid;
    ProcState state;
    uint8_t   kstack[4096];    // 内核栈（每个进程独立）
    Context   ctx;             // 保存的上下文（在内核栈上）
} Proc;

#define NR_PROC 8
extern Proc    proc_table[NR_PROC];
extern Proc   *current_proc;
```

**2. 实现 `sos/kernel/proc.c`**

**`proc_create` —— 创建一个新进程：**

```c
Proc *proc_create(const char *name) {
    // 1. 找一个空闲 PCB 槽位
    Proc *p = NULL;
    for (int i = 0; i < NR_PROC; i++) {
        if (proc_table[i].state == DEAD) { p = &proc_table[i]; break; }
    }
    assert(p != NULL);

    // 2. 从 Ramdisk 加载 ELF（解析 ELF Header，把 PT_LOAD 段复制到对应物理地址）
    size_t sz;
    uint8_t *elf_data = ramdisk_get(name, &sz);
    uint32_t entry = load_elf_from_memory(elf_data);  // 自己实现这个函数

    // 3. 初始化上下文：设置 PC 为入口，其他寄存器清零
    memset(&p->ctx, 0, sizeof(Context));
    p->ctx.pc = entry;
    // sp 指向该程序的用户栈（用一段固定内存，简单实现：用加载地址 - 4KB）
    p->ctx.gpr[2] = /* sp */ entry - 4096;  // 示意，实际需要根据 ELF 布局确定

    // 4. 设置状态
    p->state = RUNNABLE;
    p->pid   = (p - proc_table) + 1;

    return p;
}
```

> `load_elf_from_memory`：和 A 的 ELF 加载器逻辑相似，但从内存数组而非文件读取，自己实现或复用 A 的代码。

**3. 实现 `sos/kernel/schedule.c`（Round-Robin 调度器）**

```c
Context *schedule(Context *cur) {
    // 保存当前进程的上下文
    if (current_proc && current_proc->state == RUNNING) {
        current_proc->ctx = *cur;
        current_proc->state = RUNNABLE;
    }

    // 找下一个 RUNNABLE 进程（循环遍历）
    int start = current_proc ? (current_proc - proc_table + 1) % NR_PROC : 0;
    for (int i = 0; i < NR_PROC; i++) {
        int idx = (start + i) % NR_PROC;
        if (proc_table[idx].state == RUNNABLE) {
            current_proc = &proc_table[idx];
            current_proc->state = RUNNING;
            return &current_proc->ctx;  // 返回新进程的 Context
        }
    }
    // 没有可运行进程：空转（或 halt）
    printf("No runnable process!\n");
    while(1);
    return cur;
}
```

**4. 在 `kernel_main` 中启动进程**

```c
void kernel_main() {
    ramdisk_init();
    proc_init();   // 初始化 proc_table（全部标记为 DEAD）

    proc_create("uprog1");
    proc_create("uprog2");

    // 手动触发一次调度（进入第一个进程）
    // 构造一个空的 Context，让 schedule 选出第一个进程并返回其 Context
    Context dummy = {0};
    Context *next = schedule(&dummy);
    // 把 next 的 pc 写入 mepc，恢复寄存器，mret 进入用户进程
    // （这部分在 start.S 里完成，kernel_main 实际上"不返回"）
}
```

### 检查点
- [ ] `proc_create("uprog1")` 后，PCB 中 `ctx.pc` 等于 uprog1 的入口地址
- [ ] 调度器在两个进程之间切换，终端交替出现 "Program 1" 和 "Program 2"

---

## 阶段总结

完成本阶段后，SOS 内核具备：

| 能力 | 文件 |
|------|------|
| Trap 入口（保存/恢复上下文） | `trap_entry.S` |
| Trap 分发 | `trap.c` |
| 系统调用 write/exit/yield | `syscall.c` |
| 内存文件系统 | `fs/ramdisk.c` |
| 进程创建与 Round-Robin 调度 | `proc.c`, `schedule.c` |

**你现在能回答：**
- 为什么 ECALL 的 Trap Handler 要把 `mepc += 4`？
- Round-Robin 调度器的"公平性"体现在哪里？有什么缺点？
- 上下文切换的本质是什么？为什么 `trap_handler` 返回一个指针而不是 void？
- Ramdisk 和真正的文件系统有什么区别？

---

## 进入下一步的前提

满足以下所有条件后，等待成员 A 完成 PA4-A，再一起进入「联调4」：

- [ ] 所有检查点通过
- [ ] 两个用户进程能在 native 测试框架中模拟调度（逻辑通过）
- [ ] `sos.elf` 用 riscv32 编译通过
- [ ] 代码已推送到 `feat/pa4-sos` 分支

---

## 常见问题

**Q：`trap_entry.S` 汇编里保存 sp 时，保存的是陷入前的值还是陷入后的值？**  
A：CPU 跳转到 `trap_entry` 后，sp 还没有被修改，但我们第一行做了 `addi sp, sp, -136`，所以 sp 已经变了。需要把"原来的 sp"（= 当前 sp + 136）保存到 `ctx.gpr[2]`，否则 `mret` 恢复后 sp 会被破坏。

**Q：两个进程的用户栈会不会冲突？**  
A：如果用不同的加载地址（`0x80040000` 和 `0x80080000`），栈分别在加载地址下方，只要每个程序的栈不超过 4KB 就不会冲突。实际操作系统用页表隔离，我们这里用物理地址分离来简单处理。

**Q：`schedule` 返回新进程的 `Context *` 后，怎么用它恢复寄存器？**  
A：`trap_entry.S` 的 restore 部分直接从 `a0`（`trap_handler` 的返回值）指向的 Context 加载寄存器。关键是：先把 `a0` 移到 `sp`（`mv sp, a0`），然后从新的 `sp` 位置 `lw` 所有寄存器。这就实现了：返回到的是新进程的上下文，而不是原来进程的上下文。
