# 实验手册 · PA4-A：中断与陷入

> **本手册使用者：成员 A**  
> **阶段目标：** 在 SCore 中实现 RISC-V 的 Trap 机制（CSR 寄存器、ECALL、MRET、非法指令）和定时器中断，使 CPU 能在异常/中断发生时跳转到处理程序，并在处理结束后正确返回。  
> **预计时间：** 3～4 天  
> **前置条件：** 联调3 完成（tag `v3.0-pa3`）  
> **本阶段涉及文件：**
> ```
> score/
> ├── include/
> │   ├── cpu.h           ← 修改（加入 CSR 寄存器字段）
> │   └── trap.h          ← 本阶段新建（与 B 共享的 Trap 接口常量）
> └── src/
>     ├── cpu/
>     │   ├── cpu.cpp     ← 修改（加入中断检测逻辑）
>     │   └── isa/
>     │       └── exec.cpp ← 修改（实现 CSR 指令、MRET、完善 ECALL）
>     └── device/
>         └── timer.cpp   ← 修改（加入定时器中断触发）
> ```
> **本阶段不涉及：** 操作系统的 Trap Handler 代码由成员 B 负责（`sos/`）。你只需要让 CPU 跳到正确的地址，B 的代码在那里等着。

---

## 实验一：CSR 寄存器

### 目标
在 `cpu.h` 的 `CPU_state` 结构体中加入机器模式（Machine Mode）CSR 寄存器，实现 CSR 相关指令（`CSRRW`、`CSRRS`、`CSRRC`、`CSRRWI`、`CSRRSI`、`CSRRCI`）。

### 背景知识

**为什么需要 CSR？**  
RISC-V 用控制状态寄存器（Control and Status Registers）管理 CPU 的特权状态。Trap 处理离不开以下几个 CSR：

| CSR 名 | CSR 地址 | 作用 |
|--------|---------|------|
| `mstatus` | 0x300 | 机器状态：包含 MIE（全局中断使能位）、MPIE（陷入前的 IE 值）、MPP（陷入前的特权级） |
| `mtvec` | 0x305 | Trap Vector：Trap 处理程序的入口地址。操作系统在启动时设置这个寄存器 |
| `mepc` | 0x341 | Machine Exception PC：发生 Trap 时，保存当前 PC 到这里，`mret` 时从这里恢复 |
| `mcause` | 0x342 | 陷入原因：bit31=1 表示中断，bit31=0 表示异常；低位是具体原因码 |
| `mip` | 0x344 | 待处理中断标志（Machine Interrupt Pending） |
| `mie` | 0x304 | 中断使能掩码（Machine Interrupt Enable） |

**`mstatus` 的关键位：**

```
bit 3: MIE  —— 全局中断使能（1=允许中断）
bit 7: MPIE —— 陷入前的 MIE 值（陷入时保存，mret 时恢复）
bit[12:11]: MPP —— 陷入前的特权级（我们全用 Machine Mode，固定为 0b11）
```

**陷入时 CPU 自动做的事（硬件行为，你要在软件中模拟）：**

```
1. mepc  ← PC（保存当前 PC）
2. mcause ← 陷入原因码
3. mstatus.MPIE ← mstatus.MIE  （保存中断使能状态）
4. mstatus.MIE  ← 0             （关闭中断，防止嵌套）
5. PC   ← mtvec                 （跳转到 Trap Handler）
```

**`mret` 时 CPU 自动做的事：**

```
1. PC           ← mepc          （从保存的 PC 恢复）
2. mstatus.MIE  ← mstatus.MPIE  （恢复中断使能）
3. mstatus.MPIE ← 1             （恢复 MPIE 为 1）
```

### 步骤

**1. 在 `cpu.h` 的 `CPU_state` 结构体中加入 CSR**

```cpp
struct CPU_state {
    uint32_t gpr[32];
    uint32_t pc;

    // Machine-mode CSRs
    struct {
        uint32_t mstatus;
        uint32_t mtvec;
        uint32_t mepc;
        uint32_t mcause;
        uint32_t mip;
        uint32_t mie;
    } csr;
};
```

**2. 实现 CSR 读写辅助函数**

在 `exec.cpp` 中添加：

```cpp
// 根据 CSR 地址读取对应字段
uint32_t csr_read(uint32_t addr) {
    switch (addr) {
    case 0x300: return cpu.csr.mstatus;
    case 0x304: return cpu.csr.mie;
    case 0x305: return cpu.csr.mtvec;
    case 0x341: return cpu.csr.mepc;
    case 0x342: return cpu.csr.mcause;
    case 0x344: return cpu.csr.mip;
    default:
        fprintf(stderr, "未知 CSR 地址：0x%03x\n", addr);
        return 0;
    }
}

void csr_write(uint32_t addr, uint32_t val) {
    switch (addr) {
    case 0x300: cpu.csr.mstatus = val; break;
    case 0x304: cpu.csr.mie     = val; break;
    case 0x305: cpu.csr.mtvec   = val; break;
    case 0x341: cpu.csr.mepc    = val; break;
    case 0x342: cpu.csr.mcause  = val; break;
    case 0x344: cpu.csr.mip     = val; break;
    default:
        fprintf(stderr, "未知 CSR 地址：0x%03x\n", addr);
    }
}
```

**3. 实现 CSR 指令（opcode = 0x73，funct3 != 0）**

在 `exec.cpp` 的 `case 0x73` 中，区分 ECALL（funct3==0）和 CSR 指令（funct3==1/2/3/5/6/7）：

| funct3 | 指令 | 操作 |
|--------|------|------|
| 1 | CSRRW | `t = csr_read(csr); csr_write(csr, rs1); rd = t` |
| 2 | CSRRS | `rd = csr_read(csr); csr_write(csr, csr_read(csr) \| rs1)` |
| 3 | CSRRC | `rd = csr_read(csr); csr_write(csr, csr_read(csr) & ~rs1)` |
| 5 | CSRRWI | `rd = csr_read(csr); csr_write(csr, zimm)`（zimm = RS1字段的值，不是寄存器） |
| 6 | CSRRSI | `rd = csr_read(csr); csr_write(csr, csr_read(csr) \| zimm)` |
| 7 | CSRRCI | `rd = csr_read(csr); csr_write(csr, csr_read(csr) & ~zimm)` |

> `csr` = bits[31:20] of instruction（12位 CSR 地址）

**4. 实现 `mret` 指令**

`mret` 的编码：`opcode=0x73, funct3=0, funct7=0x18, rs2=0x02`。在 ECALL 的判断后加：

```cpp
// 判断是否是 mret
if ((instr >> 7) == (0b0011000000100000000000 >> 7)) {
    // mret
    cpu.pc = cpu.csr.mepc;
    // 恢复 mstatus 中的 MIE
    uint32_t mpie = (cpu.csr.mstatus >> 7) & 1;
    cpu.csr.mstatus &= ~(1u << 3);
    cpu.csr.mstatus |= (mpie << 3);
    cpu.csr.mstatus |= (1u << 7);  // MPIE 恢复为 1
    return;
}
```

> 或者用更清晰的方式：`if (instr == 0x30200073)`（mret 的固定编码）。

### 检查点
- [ ] `csrw mtvec, t0`（先 `li t0, 0x80001000`）后，`x 1 0xa0001000`（或者用新增 `info csr` 命令）看到 mtvec 被设置
- [ ] CSR 指令编译进 cpu-tests，全部 PASS

---

## 实验二：Trap 处理

### 目标
实现完整的 Trap 进入流程：ECALL、非法指令都能触发 Trap，CPU 跳转到 `mtvec` 指定的地址。

### 背景知识

**Trap 的两种来源：**

1. **同步异常（Exception）：** 由当前指令触发，如 `ECALL`（系统调用）、非法指令（未知 opcode）。异常发生时，`mepc` 保存的是**触发异常的指令的地址**（对于 ECALL，`mepc = ecall 指令的地址`，这样 `mret` 后会重新执行 ecall，所以 OS 处理完系统调用后要手动把 `mepc += 4` 跳过）。

2. **异步中断（Interrupt）：** 由外部事件触发，与当前执行的指令无关，如定时器中断。中断发生时，`mepc` 保存的是**被中断打断的那条指令的地址**（中断处理后 `mret` 继续执行那条指令）。

**`mcause` 的编码：**

```
bit31 = 1  → 中断，低位编码：1=Supervisor software, 3=Machine software,
              5=Supervisor timer, 7=Machine timer, 9=Supervisor external...
bit31 = 0  → 异常，低位编码：0=Instruction address misaligned,
              2=Illegal instruction, 8=ECALL from U-mode, 11=ECALL from M-mode...
```

我们用 M-mode ECALL（`mcause = 11`）和 Machine timer interrupt（`mcause = 0x80000007`）。

### 步骤

**1. 封装 `do_trap(cause, pc)` 函数**

```cpp
void do_trap(uint32_t cause, uint32_t trap_pc) {
    // 按照 RISC-V 规范，依次更新 CSR
    cpu.csr.mepc   = trap_pc;
    cpu.csr.mcause = cause;
    // 保存并清除 MIE
    uint32_t mie_bit = (cpu.csr.mstatus >> 3) & 1;
    cpu.csr.mstatus &= ~(1u << 3);   // 清除 MIE
    cpu.csr.mstatus |=  (mie_bit << 7); // MPIE ← MIE
    // 跳转到 mtvec
    // 注意 mtvec 低2位是模式位（0=direct, 1=vectored），取地址部分
    cpu.pc = cpu.csr.mtvec & ~0x3u;
}
```

**2. 修改 ECALL 处理**

在 `exec.cpp` 的 ECALL 分支：不再直接检查 `a7 == 93`，而是调用 `do_trap(11, this_pc)`（M-mode ECALL，cause=11）：

```cpp
case 0x73:
    if (FUNCT3(instr) == 0 && RD(instr) == 0 && RS1(instr) == 0
        && instr != 0x30200073 /* mret */) {
        // ECALL
        do_trap(11, this_pc);
        return;
    }
    // ... mret 和 CSR 指令 ...
```

> ⚠️ 此时 cpu-tests 里的测试会失败！因为原来 cpu-tests 里的 `ECALL` 直接被 SCore 处理（打印 GOOD/BAD TRAP），现在改为跳转到 `mtvec`，而 `mtvec` 初始值为 0，会跳到无效地址。
>
> **解决方案：** 在 SCore 的 `do_trap` 中加一个 `mtvec == 0` 的特殊处理——如果 `mtvec` 为 0（说明操作系统还没设置），则回退到原来的直接处理（打印 GOOD/BAD TRAP）。这样 cpu-tests 继续有效：

```cpp
void do_trap(uint32_t cause, uint32_t trap_pc) {
    if (cpu.csr.mtvec == 0) {
        // mtvec 未设置，用旧方式处理（兼容 cpu-tests）
        if (cause == 11) {  // ECALL
            if (cpu.gpr[17] == 93) {  // a7=93, exit
                uint32_t code = cpu.gpr[10];
                if (code == 0) { printf("HIT GOOD TRAP\n"); cpu_state = CPU_END; }
                else { printf("HIT BAD TRAP: %d\n", code); cpu_state = CPU_ABORT; }
            }
        }
        return;
    }
    // ... 正常 Trap 处理 ...
}
```

**3. 修改非法指令处理**

```cpp
default:  // 非法指令
    fprintf(stderr, "非法指令 @ 0x%08x: 0x%08x\n", this_pc, instr);
    do_trap(2, this_pc);  // cause=2: Illegal instruction
    break;
```

### 检查点
- [ ] cpu-tests 仍然全部 PASS（`mtvec == 0` 兼容路径工作正常）
- [ ] 写一个简单汇编：设置 `mtvec` → 执行 `ecall` → 验证 PC 跳转到 `mtvec` 地址
- [ ] 验证 `mepc` 保存了 ECALL 指令的地址

---

## 实验三：定时器中断

### 目标
在 SCore 中实现定时器中断：每隔一段时间（约10ms），如果 CPU 的 MIE 和 MTIE 都置位，产生一次定时器中断。

### 背景知识

**RISC-V 定时器中断：**  
RISC-V 机器模式定时器中断的 cause 码是 `0x80000007`（bit31=1 表示中断，低位=7 表示 Machine Timer）。

中断与异常的区别：
- 异常是**同步**的：在执行某条指令时立即发生
- 中断是**异步**的：可以在任意两条指令之间发生

SCore 中，我们在每次 `cpu_exec` 的指令循环里检查是否有待处理的中断：

```
if (mstatus.MIE && mie.MTIE && mip.MTIP) {
    do_trap(0x80000007, cpu.pc);  // mepc 保存的是"被打断的下一条指令"
}
```

### 步骤

**1. 在 `timer.cpp` 中添加中断触发逻辑**

每隔约 10ms（可以按指令数来估算，也可以按真实时间）设置 `mip.MTIP`：

```cpp
// bit 7 of mip: MTIP (Machine Timer Interrupt Pending)
#define MIP_MTIP  (1u << 7)
#define MIE_MTIE  (1u << 7)

static uint64_t last_tick_ms = 0;

void timer_tick() {
    // 读取当前时间
    uint64_t now_ms = get_ms();
    if (now_ms - last_tick_ms >= 10) {  // 10ms 一次
        last_tick_ms = now_ms;
        cpu.csr.mip |= MIP_MTIP;  // 设置定时器中断挂起位
    }
}
```

**2. 在 `cpu_exec` 中检查并处理中断**

在每条指令执行之后，检查中断：

```cpp
void cpu_exec(uint64_t n) {
    for (uint64_t i = 0; i < n && cpu_state == CPU_RUNNING; i++) {
        timer_tick();  // 更新定时器状态

        // 检查中断
        bool mie  = (cpu.csr.mstatus >> 3) & 1;
        bool mtie = (cpu.csr.mie   >> 7) & 1;
        bool mtip = (cpu.csr.mip   >> 7) & 1;
        if (mie && mtie && mtip) {
            cpu.csr.mip &= ~MIP_MTIP;  // 清除 pending bit
            do_trap(0x80000007, cpu.pc);
            continue;
        }

        // 正常取指执行
        uint32_t instr   = paddr_read(cpu.pc, 4);
        uint32_t this_pc = cpu.pc;
        cpu.pc += 4;
        exec_once(instr, this_pc);
        cpu.gpr[0] = 0;
    }
}
```

**3. 编写 `score/include/trap.h`（与 B 共享）**

```cpp
// trap.h: SCore 与 SOS 共同使用的 Trap 相关常量
#pragma once

// mcause 值
#define TRAP_ECALL_M      11          // ECALL from M-mode
#define TRAP_ILLEGAL_INST 2           // Illegal instruction
#define TRAP_TIMER_INT    0x80000007  // Machine timer interrupt

// mstatus 位定义
#define MSTATUS_MIE   (1u << 3)
#define MSTATUS_MPIE  (1u << 7)

// mie/mip 位定义
#define MIE_MTIE      (1u << 7)
#define MIP_MTIP      (1u << 7)
```

把这个文件提交给 B，B 的 SOS 汇编代码会用到这些常量。

### 检查点
- [ ] 写汇编测试：设置 mtvec → 开启 MIE 和 MTIE → 循环等待 → 10ms 后 PC 跳转到 mtvec
- [ ] `mret` 后能正确返回被打断的指令继续执行
- [ ] 连续产生多次定时器中断，系统不崩溃

---

## 阶段总结

完成本阶段后，SCore 具备：

| 能力 | 实现 |
|------|------|
| CSR 读写（6条指令） | CSRRW/S/C 及其立即数版本 |
| 同步 Trap（ECALL、非法指令） | `do_trap()` + `mret` |
| 异步中断（定时器） | `timer_tick()` + 中断检测 |
| 兼容 cpu-tests | `mtvec==0` 时回退旧行为 |

**你现在能回答：**
- RISC-V 的 `mstatus` 寄存器中 MIE 和 MPIE 各自的作用是什么？
- ECALL 的 `mepc` 保存的是哪条指令的地址？OS 返回时为什么要 `mepc += 4`？
- 定时器中断和 ECALL 的 `mcause` 如何区分？
- 为什么中断检测要放在"取指之前"而不是"执行之后"？

---

## 进入下一步的前提

满足以下所有条件后，等待成员 B 完成 PA4-B，再一起进入「联调4」：

- [ ] 所有检查点通过
- [ ] `trap.h` 已提交，B 可以使用
- [ ] cpu-tests 仍然全部 PASS
- [ ] 代码已推送到 `feat/pa4-trap` 分支

---

## 常见问题

**Q：实现 do_trap 后，SCore 运行任何程序都立刻跳飞（PC 跳到 0）？**  
A：`mtvec` 初始值为 0。程序运行之前，操作系统应该先设置 `mtvec`（用 `csrw mtvec, t0`）。如果你在 cpu-tests 里没有设置 `mtvec` 就触发了 ECALL，就会跳到地址 0，访问无效内存。这就是为什么需要 `mtvec==0` 的兼容路径。

**Q：定时器中断后，`mret` 回来程序还是正确的吗？**  
A：关键是 `mepc` 保存的是哪条指令。中断发生在取指之前，`mepc = cpu.pc`（下一条待执行的指令地址）。`mret` 后 PC 恢复为 `mepc`，从那条指令继续执行——这是正确的。

**Q：`mret` 的编码判断感觉不优雅，有更好的方法吗？**  
A：可以在 SYSTEM（0x73）的 case 中按 funct12 字段来区分：`funct12 = bits[31:20]`。`mret` 的 funct12 = `0b001100000010 = 0x302`。这样更符合规范的写法：`if (FUNCT12(instr) == 0x302) { /* mret */ }`。
