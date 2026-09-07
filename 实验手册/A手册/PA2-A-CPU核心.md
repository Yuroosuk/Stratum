# 实验手册 · PA2-A：CPU 核心

> **本手册使用者：成员 A**  
> **阶段目标：** 实现物理内存、ELF 加载器、RV32I 完整指令集执行，通过全部 cpu-test 用例，`si` 命令能真正单步执行指令。  
> **预计时间：** 4～5 天  
> **前置条件：** 联调1 完成（tag `v1.0-pa1`）  
> **本阶段涉及文件：**
> ```
> score/
> ├── include/
> │   ├── cpu.h           ← 修改（加入执行状态枚举）
> │   └── memory.h        ← 不变（接口不变，实现替换）
> └── src/
>     ├── cpu/
>     │   ├── cpu.cpp     ← 修改（加入 cpu_exec 函数）
>     │   └── isa/
>     │       ├── riscv32.h   ← 本阶段新建（指令解码宏）
>     │       └── exec.cpp    ← 本阶段新建（所有指令的执行函数）
>     ├── memory/
>     │   └── memory.cpp  ← 本阶段修改（真实内存实现，替换桩）
>     └── monitor/
>         ├── sdb.cpp     ← 修改（si 命令接入真实 cpu_exec）
>         └── elf.cpp     ← 本阶段新建（ELF 加载器）
> ```
> **本阶段不涉及：** SHAL、MMIO、SDL2 — 这些由成员 B 和 PA3 完成。

---

## 实验一：物理内存

### 目标
用一个 `uint8_t` 数组模拟 128MB 的物理内存，实现 `paddr_read` 和 `paddr_write`，替换 PA1 中的桩实现。

### 背景知识

**物理地址空间：**  
SCore 模拟的 RISC-V CPU 把物理地址 `0x80000000` 作为内存起始地址（这是标准 RISC-V 的约定，裸机程序从这个地址开始运行）。

我们的物理内存大小为 128MB（足够运行小型操作系统），因此内存覆盖范围是：
```
0x80000000 ~ 0x87FFFFFF
```

**地址转换：**  
宿主机的 `uint8_t mem[128*1024*1024]` 数组下标从 0 开始，但 Guest 物理地址从 `0x80000000` 开始。因此访问 Guest 地址 `addr` 对应数组下标 `addr - 0x80000000`。

这个转换用一个宏来封装：

```c
#define MEM_BASE  0x80000000u
#define MEM_SIZE  (128 * 1024 * 1024u)
#define GUEST_TO_HOST(paddr)  (mem + (paddr) - MEM_BASE)
```

**字节序（endianness）：**  
RISC-V 是小端序（little-endian）：一个 32 位整数 `0x12345678` 在内存中按字节存储为 `78 56 34 12`（低字节在低地址）。我们的宿主机（x86）也是小端序，因此直接用指针转换就对了，不需要字节交换。

### 步骤

**1. 在 `score/src/memory/memory.cpp` 中定义内存数组**

```cpp
static uint8_t mem[128 * 1024 * 1024];
```

**2. 实现 `paddr_read`**

`len` 参数是字节数，可以是 1、2 或 4。利用 `GUEST_TO_HOST` 宏得到宿主机指针，然后转换成对应类型读取：

思考：如何用一条语句同时处理 `len = 1/2/4`？  
提示：`memcpy` + 一个局部变量，或者 `switch(len)`。

**3. 实现 `paddr_write`**

同理，把数据写入对应地址。注意：`len < 4` 时只写 `len` 个字节，不能多写。

**4. 加入边界检查**

如果 `addr < MEM_BASE` 或 `addr + len > MEM_BASE + MEM_SIZE`，打印错误并 `abort()`：

```cpp
void check_addr(uint32_t addr, int len) {
    if (addr < MEM_BASE || addr - MEM_BASE + len > MEM_SIZE) {
        fprintf(stderr, "非法物理地址访问：0x%08x (len=%d)\n", addr, len);
        abort();
    }
}
```

### 检查点
- [ ] `paddr_write(0x80000000, 4, 0x12345678)` 写入后，`paddr_read(0x80000000, 4)` 返回 `0x12345678`
- [ ] `paddr_read(0x80000000, 1)` 返回 `0x78`（小端序，低字节）
- [ ] `paddr_read(0x7fffffff, 4)` 触发 abort，打印错误信息

---

## 实验二：ELF 加载器

### 目标
把一个 RISC-V 32 位 ELF 可执行文件加载到模拟内存中，并设置 CPU 的 PC 为程序入口点。

### 背景知识

**ELF 格式的关键结构：**  
ELF（Executable and Linkable Format）是 Linux 下可执行文件的标准格式。你只需要关注其中两个部分：

1. **ELF Header（文件头，52 字节）：** 描述文件的整体信息，我们关心：
   - `e_ident[4]` = `{'E','L','F'}` 魔数验证
   - `e_entry`：程序入口地址（把这个值赋给 `cpu.pc`）
   - `e_phoff`：Program Header Table 的文件偏移
   - `e_phnum`：Program Header 条目数量

2. **Program Header（程序头）：** 描述如何把文件中的段加载到内存。只需要处理 `p_type == PT_LOAD` 的段：
   - `p_offset`：段数据在文件中的偏移
   - `p_paddr`：段应该加载到的物理地址
   - `p_filesz`：文件中的字节数（从文件复制这么多字节）
   - `p_memsz`：内存中占用的字节数（`p_memsz - p_filesz` 字节用 0 填充，即 BSS 段）

**相关头文件：**  
POSIX 系统提供了 ELF 结构体定义：`#include <elf.h>`，其中 `Elf32_Ehdr` 是 ELF 文件头，`Elf32_Phdr` 是 Program Header。

### 步骤

**1. 编写 `score/src/monitor/elf.cpp`**

函数签名：

```cpp
// 加载 ELF 文件到内存，设置 cpu.pc，返回是否成功
bool load_elf(const char *filename);
```

算法流程：

```
1. 打开文件（fopen，二进制读）
2. 读 ELF Header（sizeof(Elf32_Ehdr) 字节）
3. 验证魔数（e_ident[0..3] == {0x7f, 'E', 'L', 'F'}）
4. 验证是 32 位（e_ident[EI_CLASS] == ELFCLASS32）
5. 验证是 RISC-V（e_machine == EM_RISCV）
6. 设置 cpu.pc = ehdr.e_entry
7. 遍历所有 Program Header（共 e_phnum 个，每个大小 sizeof(Elf32_Phdr)）：
   for each phdr in program headers:
       fseek 到 e_phoff + i * sizeof(Elf32_Phdr) 读取 phdr
       if phdr.p_type != PT_LOAD: continue
       fseek 到 phdr.p_offset
       host_ptr = GUEST_TO_HOST(phdr.p_paddr)
       fread(host_ptr, 1, phdr.p_filesz, f)
       memset(host_ptr + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz)
8. 关闭文件，返回 true
```

**2. 在 `main.cpp` 中接入 ELF 加载**

SCore 从命令行参数接收 ELF 文件路径：

```cpp
int main(int argc, char *argv[]) {
    printf("Stratum-Core (SCore) - RISC-V 32-bit Simulator\n");
    if (argc < 2) {
        printf("Usage: %s <elf_file>\n", argv[0]);
        return 1;
    }
    if (!load_elf(argv[1])) {
        fprintf(stderr, "加载 ELF 失败：%s\n", argv[1]);
        return 1;
    }
    printf("ELF 已加载，入口地址：0x%08x\n", cpu.pc);
    sdb_mainloop();
    return 0;
}
```

**3. 编写一个最小测试 ELF**

在 `stratum/cpu-tests/` 目录下用汇编写一个什么都不做的程序：

```asm
# stratum/cpu-tests/start.S
.section .text
.global _start
_start:
    nop        # 什么都不做
    j _start   # 死循环
```

用 RISC-V 工具链编译：

```bash
riscv32-unknown-elf-gcc -nostdlib -Ttext 0x80000000 -o dummy.elf cpu-tests/start.S
riscv32-unknown-elf-objdump -d dummy.elf   # 查看反汇编，确认内容
```

然后运行：

```bash
./score/score dummy.elf
# 期望：打印入口地址 0x80000000，出现 (sdb) 提示符
```

### 检查点
- [ ] 加载 `dummy.elf` 时打印正确的入口地址 `0x80000000`
- [ ] `info r` 看到 PC = 0x80000000
- [ ] `x 1 0x80000000` 看到 `nop` 的机器码（RISC-V `nop` = `addi x0,x0,0` = `0x00000013`）

---

## 实验三：指令解码与执行框架

### 目标
实现 RV32I 指令集的解码宏和执行框架，让 `cpu_exec(n)` 能正确取指、解码、执行 n 条指令。

### 背景知识

**RV32I 指令格式：**  
RISC-V 的所有指令都是固定 32 位（4 字节）。不同指令类型的字段位置不同，但以下字段的位置对所有类型都相同：

```
bits[6:0]   = opcode（7位，决定指令大类）
bits[11:7]  = rd（目标寄存器）
bits[14:12] = funct3（进一步区分同一 opcode 下的不同指令）
bits[19:15] = rs1（源寄存器1）
bits[24:20] = rs2（源寄存器2）
bits[31:25] = funct7（进一步区分，用于区分 ADD 和 SUB 等）
```

**提取位字段的宏：**

```cpp
// 提取 instr 的 bits[hi:lo]（共 hi-lo+1 位）
#define BITS(instr, hi, lo) (((instr) >> (lo)) & ((1u << ((hi)-(lo)+1)) - 1))

// 提取各字段
#define OPCODE(i)  BITS(i, 6, 0)
#define RD(i)      BITS(i, 11, 7)
#define FUNCT3(i)  BITS(i, 14, 12)
#define RS1(i)     BITS(i, 19, 15)
#define RS2(i)     BITS(i, 24, 20)
#define FUNCT7(i)  BITS(i, 31, 25)
```

**立即数的符号扩展：**  
RV32I 的立即数会散落在指令的不同位，还需要符号扩展到 32 位。例如 I 型立即数：`bits[31:20]`，12 位有符号数。

```cpp
// 12位符号扩展到32位
#define SEXT12(x)  ((int32_t)(((uint32_t)(x) << 20) >> 20))
```

原理：左移 20 位使符号位到 bit31，然后右移 20 位（算术右移，C++ 中有符号整数右移保持符号位）。

### 步骤

**1. 在 `score/src/cpu/isa/riscv32.h` 中定义解码宏**

把上面所有的位提取宏和各类型立即数提取宏写进这个头文件。每种指令格式（I、S、B、U、J）的立即数提取都不一样，参考 RISC-V ISA 规范手册（可以搜索"RISC-V unprivileged spec"）。

**2. 在 `score/src/cpu/cpu.cpp` 中实现 `cpu_exec`**

```cpp
// 执行状态枚举（加入 cpu.h）
enum CpuState { CPU_RUNNING, CPU_STOPPED, CPU_END, CPU_ABORT };
extern CpuState cpu_state;

void cpu_exec(uint64_t n) {
    for (uint64_t i = 0; i < n && cpu_state == CPU_RUNNING; i++) {
        // 1. 取指
        uint32_t instr = paddr_read(cpu.pc, 4);

        // 2. 保存 PC，用于计算 PC+4
        uint32_t this_pc = cpu.pc;
        cpu.pc += 4;

        // 3. 检查 watchpoint（联调1已有此接口）
        //    if (wp_check()) { cpu_state = CPU_STOPPED; break; }

        // 4. 解码并执行
        exec_once(instr, this_pc);

        // 5. 强制保持 x0 = 0
        cpu.gpr[0] = 0;
    }
}
```

**3. 实现 `exec_once`（`exec.cpp`）**

这是工作量最大的部分，按 opcode 分发：

```
switch(OPCODE(instr)):
    case 0x33:  // R-type（ADD SUB AND OR XOR SLL SRL SRA SLT SLTU）
        switch(FUNCT3(instr)):
            case 0x0:
                if FUNCT7 == 0: ADD(RD, RS1, RS2)
                if FUNCT7 == 32: SUB(RD, RS1, RS2)
            case 0x1: SLL
            case 0x2: SLT
            ... 以此类推

    case 0x13:  // I-type ALU（ADDI SLTI SLTIU XORI ORI ANDI SLLI SRLI SRAI）

    case 0x03:  // Load（LB LH LW LBU LHU）

    case 0x23:  // Store（SB SH SW）

    case 0x63:  // Branch（BEQ BNE BLT BGE BLTU BGEU）

    case 0x6F:  // JAL

    case 0x67:  // JALR

    case 0x37:  // LUI

    case 0x17:  // AUIPC

    case 0x73:  // SYSTEM（ECALL — 先打印占位信息，PA4 实现）

    default:    // 非法指令
        fprintf(stderr, "非法指令：0x%08x @ PC=0x%08x\n", instr, this_pc);
        cpu_state = CPU_ABORT;
```

> **建议实现顺序：** 先实现 ADDI/ADD，跑通 `add-test`，再依次加入其他指令。不要试图一次性写完所有指令再测试，这样出错很难定位。

**4. 接入 `si` 命令**

在 `sdb.cpp` 的 `cmd_si` 函数中，将原来的占位打印换为真实调用：

```cpp
cpu_exec((n <= 0) ? 1 : n);
```

### 检查点
- [ ] 加载 `dummy.elf`（只有 `nop` + `j`），`si` 不崩溃
- [ ] `si 1` 后，`info r` 显示 PC 从 0x80000000 变为 0x80000004
- [ ] `si 2` 后，PC 回到 0x80000000（j 跳回原地）
- [ ] 触发非法指令时打印错误信息并停止，不崩溃

---

## 实验四：CPU 测试套件

### 目标
为每条 RV32I 指令写汇编测试，用自动化脚本批量运行并验证结果。

### 步骤

**1. 建立 `stratum/cpu-tests/` 目录结构**

```
cpu-tests/
├── Makefile        ← 批量编译所有 .S 文件
├── include/
│   └── riscv.h     ← PASS/FAIL 宏（用 ECALL 通知 SCore）
└── tests/
    ├── add.S       ← ADD 指令测试
    ├── addi.S      ← ADDI 指令测试
    ├── sub.S
    └── ...（每条指令一个文件）
```

**2. 定义测试结束约定**

程序用 `ECALL` 指令通知 SCore 测试结果。约定：
- `a0 = 0`：测试通过（PASS）
- `a0 != 0`：测试失败（FAIL），`a0` 是失败的测试编号

SCore 的 ECALL 处理（`exec.cpp` 中 `case 0x73`）：

```cpp
case 0x73:  // ECALL
    if (cpu.gpr[17] == 93) {  // a7 = 93 表示 exit syscall（Linux 约定）
        uint32_t code = cpu.gpr[10];  // a0 是返回码
        if (code == 0) {
            printf("HIT GOOD TRAP\n");
            cpu_state = CPU_END;
        } else {
            printf("HIT BAD TRAP: test case %d failed\n", code);
            cpu_state = CPU_ABORT;
        }
    }
    break;
```

**3. 编写测试用例模板**

```asm
# cpu-tests/tests/add.S
#include "riscv.h"

.section .text
.global _start
_start:
    # 测试1: 5 + 3 = 8
    li   t0, 5
    li   t1, 3
    add  t2, t0, t1
    li   t3, 8
    bne  t2, t3, fail_1

    # 测试2: 负数相加 -1 + 1 = 0
    li   t0, -1
    li   t1, 1
    add  t2, t0, t1
    bne  t2, zero, fail_2

    # 所有测试通过
    PASS

fail_1: FAIL(1)
fail_2: FAIL(2)
```

**4. 定义 `riscv.h` 中的宏**

```c
#define PASS     li a7, 93; li a0, 0; ecall
#define FAIL(n)  li a7, 93; li a0, n; ecall
```

**5. 编写批量测试 Makefile**

```makefile
TESTS := $(wildcard tests/*.S)
ELFS  := $(TESTS:.S=.elf)

%.elf: %.S
	riscv32-unknown-elf-gcc -nostdlib -Iinclude \
	    -Ttext 0x80000000 -o $@ $<

run: $(ELFS)
	@pass=0; fail=0; \
	for f in $^; do \
	    result=$$(../../score/score $$f 2>&1); \
	    if echo "$$result" | grep -q "HIT GOOD TRAP"; then \
	        echo "PASS: $$f"; pass=$$((pass+1)); \
	    else \
	        echo "FAIL: $$f"; fail=$$((fail+1)); \
	    fi; \
	done; \
	echo "---"; echo "$$pass passed, $$fail failed"
```

**6. 在 SCore 中支持批量模式（无交互）**

让 `main.cpp` 支持 `-b`（batch）参数，加载 ELF 后直接运行到结束，不进入 SDB：

```cpp
if (argc >= 3 && strcmp(argv[2], "-b") == 0) {
    cpu_exec(UINT64_MAX);  // 运行到 ECALL 停止
} else {
    sdb_mainloop();
}
```

批量运行时 Makefile 传递 `-b` 参数：`../../score/score $$f -b`

### 检查点
- [ ] `add.S` 测试通过（输出 `HIT GOOD TRAP`）
- [ ] 至少实现以下指令的测试：`add sub addi andi ori xori lui auipc lw sw beq bne jal jalr`
- [ ] `make run` 全部 PASS

---

## 阶段总结

完成本阶段后，你有了：

| 文件 | 内容 |
|------|------|
| `score/src/memory/memory.cpp` | 真实物理内存（替换桩） |
| `score/src/monitor/elf.cpp` | ELF 加载器 |
| `score/src/cpu/isa/exec.cpp` | RV32I 完整指令执行 |
| `stratum/cpu-tests/` | 指令测试套件 |

**你现在能回答：**
- RISC-V 指令的 opcode/rd/funct3/rs1/rs2 各在哪几位？
- ELF 文件的 PT_LOAD 段如何加载到内存？BSS 段为什么要用 0 填充？
- 为什么要强制把 x0 清零（`cpu.gpr[0] = 0`）？
- 小端序意味着什么？读写内存时需要额外处理吗？

---

## 进入下一步的前提

满足以下所有条件后，等待成员 B 完成 PA2-B，再一起进入「联调2」：

- [ ] 所有检查点通过
- [ ] `make run`（cpu-tests）全部 PASS
- [ ] 代码已推送到 `feat/pa2-cpu-core` 分支

---

## 常见问题

**Q：`si` 后 PC 没有变化？**  
A：`cpu_exec` 里 `cpu.pc += 4` 要在 `exec_once` **之前**执行（这样跳转指令可以直接覆盖 pc 的新值），或者在之后，但跳转指令要自己设置 pc，要保持一致。

**Q：`beq` 类跳转指令之后程序跑飞了？**  
A：Branch 指令的偏移量是相对于**当前 PC**（取指时的 PC），不是 pc+4。B 型立即数的位顺序是打乱的，需要按手册仔细拼接。

**Q：`load` 指令读出来的值不对？**  
A：`LBU`、`LHU` 是零扩展，`LB`、`LH` 是符号扩展，注意区分。`LB` 读出一个字节后需要先转成 `int8_t` 再转成 `uint32_t`，才能得到正确的符号扩展结果。

**Q：ELF 加载后 `x 1 0x80000000` 看到的不是预期机器码？**  
A：检查 `Ttext 0x80000000` 是否正确指定了代码段地址。另外确认 `p_paddr` 而不是 `p_vaddr` 用于地址映射（对于没有 MMU 的裸机，两者相同，但用 `p_paddr` 是正确的习惯）。
