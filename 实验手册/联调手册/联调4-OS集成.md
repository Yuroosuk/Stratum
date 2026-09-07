# 实验手册 · 联调4：OS 集成（最终联调）

> **参与者：双人共同完成**  
> **阶段目标：** 将 SOS 内核加载进 SCore，在 SCore 的 SDL2 窗口下运行，两个用户进程在定时器中断驱动下交替执行，终端交替输出 "Program 1" 和 "Program 2"，项目完成。  
> **预计时间：** 1～2 天  
> **前置条件：** PA4-A 所有检查点通过（定时器中断已验证）；PA4-B 所有检查点通过（调度逻辑已验证）

---

## 实验一：合并代码与接口对齐

### 步骤

```bash
git checkout dev
git pull origin dev
git merge --no-ff feat/pa4-trap -m "merge: pa4-a trap mechanism"
git merge --no-ff feat/pa4-sos  -m "merge: pa4-b SOS kernel"
make clean && make
```

**接口对齐检查表（合并前两人逐项确认）：**

| 接口项 | A 侧 | B 侧 | 状态 |
|--------|------|------|------|
| `TRAP_ECALL_M` = 11 | trap.h | trap_handler | ☐ |
| `TRAP_TIMER_INT` = 0x80000007 | trap.h | trap_handler | ☐ |
| `MIE_MTIE` bit 位置（bit7） | timer.cpp | start.S | ☐ |
| `do_trap` 传入的 cause 值 | exec.cpp | trap.c | ☐ |
| Context 结构体大小（136字节）| cpu.h（参考）| trap_entry.S（栈偏移）| ☐ |
| mepc 在 Context 中的偏移（128） | — | trap_entry.S | ☐ |

---

## 实验二：逐步启动 SOS

### 目标
分三个阶段让 SOS 启动，每个阶段有明确的验证点，出问题时快速定位。

### 阶段1：内核能启动，打印 banner

**1. 编译 SOS 内核 ELF**

```bash
cd sos
make sos.elf
riscv32-unknown-elf-objdump -d sos.elf | head -50  # 确认入口在 0x80000000
```

**2. 在 SCore 中运行（只运行到打印 banner）**

```bash
./score/score sos/sos.elf -b
```

**期望输出：**

```
Stratum-Core (SCore) - RISC-V 32-bit Simulator
ELF 已加载，入口地址：0x80000000
SOS kernel booted!
```

如果卡在这里了，用 SDB 单步排查（去掉 `-b` 参数）：

```
./score/score sos/sos.elf
(sdb) si 100    # 执行100条指令
(sdb) info r    # 查看所有寄存器状态
```

---

### 阶段2：Trap 机制工作

**1. 验证 `mtvec` 被正确设置**

在 `kernel_main` 打印完 banner 之后，加一条调试打印：

```c
printf("mtvec = 0x%x\n", csr_read(0x305));  // 在内核里读 mtvec（通过 C 函数）
```

或者在 SDB 中：

```
(sdb) si 1000
(sdb) info r    # 看 CSR 字段（需要扩展 info 命令支持 CSR，或者用 x 读 mtvec 内存）
```

**2. 手动触发一次 ECALL，验证跳转到 trap_entry**

在内核 banner 之后的汇编中加入一条 ecall，然后用 SDB 单步验证：

```
si 1   # 触发 ecall
info r # 查看 PC，应该跳到 mtvec 的地址（trap_entry 的地址）
```

**3. 验证 `mret` 后 PC 回到正确位置**

继续 si，执行完 trap_handler 后的 mret，确认 PC 恢复到 ecall 后的地址（`ecall_addr + 4`）。

---

### 阶段3：两进程交替运行

**1. 启动完整 SOS**

```bash
./score/score sos/sos.elf -b
```

**期望输出（交替出现）：**

```
SOS kernel booted!
Program 1 running
Program 2 running
Program 1 running
Program 2 running
...
```

两个程序的输出应该交替出现，频率约 10ms 一次（由定时器中断控制）。

**如果进程没有切换（只有一个程序在输出）：**

检查以下几点：
1. 定时器中断有没有产生？在 `do_trap` 里加 `printf` 确认
2. `schedule` 函数有没有被调用？在 `schedule` 开头加 `printf`
3. `trap_entry.S` 的 restore 部分有没有正确把 `a0` 切换为新进程的栈？

---

## 实验三：最终收尾

### 清理调试输出

确认一切正常后，去掉所有联调阶段加入的调试 `printf`（那些不属于功能的打印）。

### 最终演示验证清单

运行以下场景，全部通过才算完成：

| 场景 | 命令 | 期望结果 |
|------|------|---------|
| 加载并运行 hello.elf | `./score/score shal/apps/hello/hello.elf -b` | 正确输出 Hello 信息 |
| cpu-tests 全部通过 | `cd cpu-tests && make run` | 全 PASS |
| SOS 双进程交替 | `./score/score sos/sos.elf -b` | 交替输出两个进程的信息 |
| SDB 单步 SOS | `./score/score sos/sos.elf`，然后 `si 500` | 能单步，info r 显示正常 |
| Watchpoint 在 SOS 中工作 | SDB 中 `w $pc`，然后 `si 100` | 每次 PC 变化时触发 |

---

## 提交与打最终 tag

```bash
make clean && make   # 最后一次全量编译

# PR dev → main，Review 通过后
git checkout main
git pull origin main
git tag v4.0-final
git push origin v4.0-final
```

---

## 最终完成检查清单

- [ ] `hello.elf` 在 SCore 上正常输出
- [ ] cpu-tests 全部 PASS（不因 Trap 机制修改而破坏）
- [ ] SOS 内核能在 SCore 上启动
- [ ] 两个用户进程在定时器中断下交替调度
- [ ] SDB 的全部 8 条命令在 SOS 场景下正常工作
- [ ] 代码合并到 `main`，tag `v4.0-final` 已推送
- [ ] 两人都能用 SDB 分析 SOS 运行时的寄存器状态

---

## 回顾整个 Stratum 项目

完成所有 PA 后，你们一共构建了：

```
宿主机（Linux / macOS）
└── SCore（RISC-V CPU 模拟器，成员 A 主导）
    ├── 128MB 物理内存
    ├── RV32I 指令集执行
    ├── SDB 调试器（si / info r / x / p / w / d）
    └── MMIO 设备（UART / Timer / Keyboard / VGA）
         ↕ MMIO 地址协议
SHAL（硬件抽象层，成员 B 主导）
├── klib（printf / memcpy / malloc 等）
├── IOE 接口（Timer / Keyboard / Video）
└── 平台实现（native / riscv32）
     ↕ ECALL / mret / CSR 协议
SOS（操作系统，成员 B 主导，依赖 A 的 Trap 机制）
├── Trap 入口汇编
├── 系统调用（write / exit / yield）
├── Ramdisk 文件系统
└── Round-Robin 多进程调度
     ↕ ELF 加载 / SYS_WRITE
用户程序（uprog1 / uprog2 / typetest / hello）
```

这是一个完整的、能够运行的计算机系统栈，从 RISC-V 指令执行到多任务操作系统，每一层都由你们亲手实现。

---

*🎉 恭喜！Stratum 项目全部完成。*
