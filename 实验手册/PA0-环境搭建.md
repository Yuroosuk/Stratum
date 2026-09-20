# PA0 — 环境搭建
> 两人共用 · 必须同步完成 · 预计工时：1 天

---

## 目标

搭建统一的开发环境，建立代码仓库，确保两人能在相同的工具链下编译运行同一份代码框架，为后续所有 PA 打好基础。

**完成标志：**
- 两人机器上 `make` 均能编译通过空框架
- `./score` 启动后打印 Stratum 欢迎信息
- 两人均能正常 `git push / pull`，无冲突

---

## 一、操作系统要求

推荐使用 **Ubuntu 22.04 LTS**（虚拟机或 WSL2 均可）。macOS 也支持，但部分工具的安装方式不同，本手册以 Ubuntu 为主，macOS 差异处单独标注。

> **两人必须使用相同的 Linux 发行版大版本**，否则编译结果可能因 glibc 版本不同而产生差异。

---

## 二、安装工具链

### 2.1 基础编译工具

```bash
sudo apt update
sudo apt install -y build-essential git gdb cmake
# 验证
g++ --version      # 期望 >= 10
make --version     # 期望 >= 4.0
git --version
```

### 2.2 RISC-V 交叉编译器

PA2 才用到的，可以暂时不配置

用于将测试程序和用户程序编译为 RISC-V 32 位二进制。

```bash
sudo apt install -y gcc-riscv64-linux-gnu
# Stratum 使用 32 位裸机目标，需要额外安装或自行编译 riscv32-unknown-elf-gcc
# 推荐使用预编译包：
wget https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2024.04.12/riscv32-elf-ubuntu-22.04-gcc-nightly-2024.04.12-nightly.tar.gz
tar -xzf riscv32-elf-ubuntu-22.04-gcc-nightly-2024.04.12-nightly.tar.gz
sudo mv riscv /opt/riscv32
echo 'export PATH=$PATH:/opt/riscv32/bin' >> ~/.bashrc
source ~/.bashrc
# 验证
riscv32-unknown-elf-gcc --version
```

> macOS：使用 Homebrew 安装 `riscv-software-src/riscv/riscv-gnu-toolchain`

### 2.3 SDL2

PA3 才用到的，现在可以暂时不配置

```bash
sudo apt install -y libsdl2-dev libsdl2-image-dev
# 验证
sdl2-config --version   # 期望 >= 2.0
```

### 2.4 其他工具

```bash
sudo apt install -y python3 python3-pip xxd file
# xxd 用于查看二进制文件，file 用于验证 ELF 格式
```

---

## 三、建立 Git 仓库

### 3.1 创建仓库（由成员 A 操作，B 直接 clone）

在 GitHub / GitLab 上新建一个**私有仓库**，命名为 `stratum`。

```bash
# 成员 A 在本地初始化
git clone git@github.com:<yourname>/stratum.git
cd stratum
```

### 3.2 搭建目录骨架

```bash
mkdir -p score/src/{cpu,memory,device,monitor,utils}
mkdir -p score/include
mkdir -p shal/src/{riscv,native}
mkdir -p shal/include
mkdir -p shal/klib/src shal/klib/include
mkdir -p sos/src sos/include
mkdir -p stratum-apps/apps/hello stratum-apps/libs
# 每个目录放一个占位文件，防止 git 忽略空目录
find . -type d -empty -exec touch {}/.gitkeep \;
```

### 3.3 配置 .gitignore

```bash
cat > .gitignore << 'EOF'
build/
*.o
*.a
*.d
score/score
*.bin
*.elf
*.log
EOF
```

### 3.4 初次提交

```bash
git add .
git commit -m "chore: init project skeleton"
git push origin main
```

### 3.5 成员 B clone 仓库

```bash
git clone git@github.com:<yourname>/stratum.git
cd stratum
# 确认目录结构完整
ls
```

---

## 四、分支策略配置

在 GitHub/GitLab 上设置以下保护规则（由成员 A 在仓库设置中操作）：

| 分支 | 保护规则 |
|------|----------|
| `main` | 禁止直接 push，必须通过 PR + 对方 Review 才能合入 |
| `dev` | 允许直接 push，作为日常集成分支 |

```bash
# 两人都执行，创建 dev 分支
git checkout -b dev
git push origin dev
```

日常开发在各自的 feature 分支上进行：

```bash
# 成员 A 创建 PA1 分支
git checkout dev
git checkout -b feat/pa1-score-skeleton

# 成员 B 创建 PA1 分支
git checkout dev
git checkout -b feat/pa1-expr-parser
```

---

## 五、搭建空框架并验证编译

### 5.1 顶层 Makefile

在项目根目录创建 `Makefile`：

```makefile
.PHONY: all score shal sos clean

all: score

score:
	$(MAKE) -C score

shal:
	$(MAKE) -C shal

sos:
	$(MAKE) -C sos

clean:
	$(MAKE) -C score clean
	$(MAKE) -C shal clean
	$(MAKE) -C sos clean
```

### 5.2 SCore 子 Makefile

在 `score/Makefile` 中：

```makefile
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g
TARGET   := score
SRCS     := $(shell find src -name '*.cpp')
OBJS     := $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
```

### 5.3 最小可运行入口

创建 `score/src/main.cpp`：

```cpp
#include <cstdio>

int main(int argc, char *argv[]) {
    printf("Stratum-Core (SCore) - RISC-V 32-bit Simulator\n");
    printf("Build: " __DATE__ " " __TIME__ "\n");
    return 0;
}
```

### 5.4 验证编译

```bash
# 两人分别执行
make
./score/score
# 期望输出：
# Stratum-Core (SCore) - RISC-V 32-bit Simulator
# Build: ...
```

---

## 六、两人同步检查清单

完成本阶段后，两人各自对照检查：

- [ ] `g++ --version` 显示版本 >= 10
- [ ] `riscv32-unknown-elf-gcc --version` 正常输出
- [ ] `sdl2-config --version` 正常输出
- [ ] `make` 编译通过，无 warning
- [ ] `./score/score` 打印欢迎信息
- [ ] `git log` 能看到初始提交
- [ ] 能在自己机器上 `git push`（SSH Key 已配置）

---

## 七、提交本阶段成果

```bash
git add .
git commit -m "pa0: environment setup and project skeleton"
git push origin feat/pa1-score-skeleton   # 成员 A
# 或
git push origin feat/pa1-expr-parser      # 成员 B

# 两人分别发 PR 到 dev，对方 Review 后合入
# dev 合入 main，打 tag
git tag v0.1-pa0
git push origin v0.1-pa0
```

---

*PA0 完成后，成员 A 进入 [PA1-A-SCore调试器骨架]，成员 B 进入 [PA1-B-表达式解析]，两人独立并行开发。*
