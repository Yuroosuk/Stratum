# 实验手册 · PA3-B：SHAL IOE 接口

> **本手册使用者：成员 B**  
> **阶段目标：** 完成 SHAL 的 IOE（I/O Extension）接口，为 Timer、Keyboard、Video 三种设备提供 native 和 riscv32 两套平台实现，并编写一个打字游戏 Demo 验证所有接口。  
> **预计时间：** 3～4 天  
> **前置条件：** 联调2 完成（tag `v2.0-pa2`）；成员 A 已提交 `device.h`（包含 MMIO 地址常量和 AM 键码定义）  
> **本阶段涉及文件：**
> ```
> shal/
> ├── include/
> │   └── am.h                ← 本阶段修改（加入 IOE 接口定义）
> ├── platform/
> │   ├── native/
> │   │   └── ioe.c           ← 本阶段修改（加入 Timer/Kbd/Video）
> │   └── riscv32/
> │       └── ioe.c           ← 本阶段新建（访问 SCore MMIO 寄存器）
> └── apps/
>     └── typetest/
>         ├── Makefile        ← 本阶段新建
>         └── main.c          ← 本阶段新建（打字游戏）
> ```
> **本阶段依赖 A 的接口：** `device.h` 中的 MMIO 地址和 AM 键码定义。如 A 还未提交，先自己暂定数值，联调3时对齐。  
> **本阶段不涉及：** `score/` 中的任何代码。

---

## 实验一：扩展 SHAL 接口定义

### 目标
在 `shal/include/am.h` 中定义 IOE 相关的类型和函数接口，供所有平台实现和应用程序使用。

### 背景知识

**IOE 接口的设计原则：**  
IOE 接口应该屏蔽硬件细节。应用程序调用 `ioe_read(AM_TIMER_UPTIME, &uptime_data)` 而不是直接读某个魔法地址。这样同一份应用代码，无论运行在 native 还是 riscv32 上，都不需要修改。

**AM 的 IOE 约定：**  
IOE 以"设备 + 控制字"的方式工作：

- `ioe_init()`：初始化所有 IO 设备
- `ioe_read(int reg, void *buf)`：读取某个设备的状态到 `buf`
- `ioe_write(int reg, void *buf)`：向某个设备写入数据

`reg` 是控制字，标识设备+数据类型，`buf` 指向对应的结构体。

### 步骤

**1. 扩展 `shal/include/am.h`**

定义控制字枚举和对应的数据结构：

```c
// 控制字
typedef enum {
    AM_TIMER_UPTIME = 1,  // 读：开机至今的毫秒数
    AM_INPUT_KEYBRD,      // 读：键盘事件
    AM_VIDEO_CONFIG,      // 读：屏幕宽高
    AM_VIDEO_FBDRAW,      // 写：向屏幕绘制像素
} AMReg;

// 对应的数据结构
typedef struct { uint64_t us; } AMTimer;       // uptime，单位微秒

typedef struct {
    bool    keydown;   // true=按下, false=松开
    int     keycode;   // AM_KEY_XXX
} AMInput;

typedef struct {
    int width, height;
} AMVideoConfig;

typedef struct {
    int       x, y, w, h;   // 绘制区域（左上角坐标+宽高）
    uint32_t *pixels;        // 像素数组（ARGB，行优先）
} AMFBDraw;

// IOE 函数声明
void ioe_init();
void ioe_read(AMReg reg, void *buf);
void ioe_write(AMReg reg, void *buf);
```

**2. 把 AM 键码定义也放入 `am.h`**

把 `device.h` 里的 `AMKey` 枚举（由 A 定义）**复制**到 `am.h` 中，这样应用程序只需要包含 `am.h` 就能使用键码，不需要知道 `device.h` 的存在。

---

## 实验二：native 平台 IOE 实现

### 目标
在 `shal/platform/native/ioe.c` 中，用 Linux 系统调用实现 IOE（不依赖 SDL2，保持 native 的简洁）。

### 步骤

**1. 实现 `ioe_init()`（native 版）**

Native 不需要特殊初始化，空函数即可：

```c
void ioe_init() {
    // native 平台不需要特殊初始化
}
```

**2. 实现 `AM_TIMER_UPTIME`（native 版）**

用 `clock_gettime(CLOCK_MONOTONIC, ...)` 计算从启动到现在的微秒数：

```c
static struct timespec boot_time;
static bool   boot_inited = false;

static void timer_read(AMTimer *out) {
    if (!boot_inited) {
        clock_gettime(CLOCK_MONOTONIC, &boot_time);
        boot_inited = true;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t us = (now.tv_sec - boot_time.tv_sec) * 1000000
                + (now.tv_nsec - boot_time.tv_nsec) / 1000;
    out->us = us;
}
```

**3. 实现 `AM_INPUT_KEYBRD`（native 版）**

Native 平台没有 SDL 窗口，键盘读取用最简单的方法：`read(STDIN_FILENO, ...)`（非阻塞模式）。如果没有按键，返回 `AM_KEY_NONE`：

```c
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

static void kbd_read(AMInput *out) {
    // 设置终端为非阻塞
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    char c;
    int ret = read(STDIN_FILENO, &c, 1);

    out->keydown = (ret == 1);
    out->keycode  = (ret == 1) ? c : AM_KEY_NONE;
    // 注意：终端输入的 char 和 AMKey 码不完全对应，
    // native 平台只做简单近似（测试用）
}
```

> 注意：Native 的键盘实现很粗糙（ASCII 字符而非键码），这是可接受的。真正的测试在 riscv32 平台上（联调3）进行。

**4. 实现 `AM_VIDEO_CONFIG` 和 `AM_VIDEO_FBDRAW`（native 版）**

Native 没有显示窗口，打印到终端作为调试输出：

```c
static void video_config_read(AMVideoConfig *out) {
    out->width  = 640;
    out->height = 480;
}

static void fbdraw_write(AMFBDraw *in) {
    // native 版：只打印一条调试信息，不实际绘制
    printf("[FBDraw] x=%d y=%d w=%d h=%d\n", in->x, in->y, in->w, in->h);
}
```

**5. 实现分发函数**

```c
void ioe_read(AMReg reg, void *buf) {
    switch (reg) {
    case AM_TIMER_UPTIME:  timer_read((AMTimer *)buf);       break;
    case AM_INPUT_KEYBRD:  kbd_read((AMInput *)buf);         break;
    case AM_VIDEO_CONFIG:  video_config_read((AMVideoConfig *)buf); break;
    default: break;
    }
}

void ioe_write(AMReg reg, void *buf) {
    switch (reg) {
    case AM_VIDEO_FBDRAW: fbdraw_write((AMFBDraw *)buf); break;
    default: break;
    }
}
```

### 检查点
- [ ] native 平台编译通过
- [ ] `ioe_read(AM_TIMER_UPTIME, &t)` 返回递增的时间
- [ ] `ioe_read(AM_VIDEO_CONFIG, &cfg)` 返回 640×480

---

## 实验三：riscv32 平台 IOE 实现

### 目标
在 `shal/platform/riscv32/ioe.c` 中，通过读写 SCore 的 MMIO 地址实现 IOE，这些地址由成员 A 的 `device.h` 定义。

### 背景知识

在 riscv32 平台上，"读取计时器"就是读取 `0xa0001000` 地址；"读键盘"就是读取 `0xa0002004`。

用 C 语言访问 MMIO 地址的方式：

```c
#define REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

// 读：
uint32_t val = REG(0xa0001000);

// 写：
REG(0xa0004000 + offset) = pixel_value;
```

`volatile` 关键字防止编译器优化掉这些"看似没用的"读写操作。

### 步骤

**1. 包含 A 的地址定义**

```c
// 直接硬编码或 include device.h（联调3时对齐）
#define TIMER_LOW     0xa0001000u
#define TIMER_HIGH    0xa0001004u
#define KBD_STATUS    0xa0002000u
#define KBD_DATA      0xa0002004u
#define VGA_WIDTH_REG 0xa0003000u
#define VGA_HEIGHT_REG 0xa0003004u
#define VGA_FB_BASE   0xa0004000u
```

**2. 实现 Timer**

```c
static void timer_read(AMTimer *out) {
    uint32_t lo = REG(TIMER_LOW);     // 先读低位（同时锁存高位）
    uint32_t hi = REG(TIMER_HIGH);    // 再读高位（已被锁存）
    uint64_t ms = ((uint64_t)hi << 32) | lo;
    out->us = ms * 1000;  // 转换为微秒
}
```

**3. 实现 Keyboard**

```c
static void kbd_read(AMInput *out) {
    uint32_t status = REG(KBD_STATUS);
    if (status == 0) {
        out->keydown = false;
        out->keycode  = AM_KEY_NONE;
    } else {
        uint32_t data = REG(KBD_DATA);
        out->keydown = (data >> 31) & 1;
        out->keycode  = data & 0x7fffffff;
    }
}
```

**4. 实现 Video**

```c
static void video_config_read(AMVideoConfig *out) {
    out->width  = REG(VGA_WIDTH_REG);
    out->height = REG(VGA_HEIGHT_REG);
}

static void fbdraw_write(AMFBDraw *in) {
    // 把 pixels 数组按行写入 framebuffer
    for (int row = 0; row < in->h; row++) {
        for (int col = 0; col < in->w; col++) {
            uint32_t color   = in->pixels[row * in->w + col];
            int pixel_offset = ((in->y + row) * 640 + (in->x + col)) * 4;
            REG(VGA_FB_BASE + pixel_offset) = color;
        }
    }
}
```

> ⚠️ `fbdraw_write` 的宽度（行字节数）要从 `VGA_WIDTH_REG` 读取，或者写死 640，和 A 对齐。

---

## 实验四：打字游戏 Demo

### 目标
编写一个最简单的打字游戏：屏幕上随机出现字母，玩家按对应键消除。用这个程序验证 IOE 的三个设备全部工作正常。

### 步骤

**1. 编写 `shal/apps/typetest/main.c`**

游戏逻辑：
- 用 Timer 初始化随机种子（`rand() % 26 + AM_KEY_A`）
- 用 Video 清屏（全黑），在随机位置画一个彩色方块代表"目标字母"
- 轮询 Keyboard，读到正确按键时消除方块，显示下一个
- 每隔 5 秒没有正确输入，重新随机

为了简化，"画字母"可以只画一个纯色方块（不需要字体），颜色代表字母（如 A=红、B=绿...）。

```c
#include <am.h>
#include <klib.h>

#define BLOCK_SIZE 40

void fill_rect(int x, int y, int w, int h, uint32_t color) {
    static uint32_t buf[BLOCK_SIZE * BLOCK_SIZE];
    for (int i = 0; i < w * h; i++) buf[i] = color;
    AMFBDraw draw = {x, y, w, h, buf};
    ioe_write(AM_VIDEO_FBDRAW, &draw);
}

int main() {
    ioe_init();

    AMVideoConfig cfg;
    ioe_read(AM_VIDEO_CONFIG, &cfg);

    // 清屏
    fill_rect(0, 0, cfg.width, cfg.height, 0x00000000);

    int target_key = AM_KEY_A;  // 从 A 开始
    int x = (cfg.width  - BLOCK_SIZE) / 2;
    int y = (cfg.height - BLOCK_SIZE) / 2;

    uint32_t colors[] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffff00};
    fill_rect(x, y, BLOCK_SIZE, BLOCK_SIZE, colors[target_key % 4]);

    while (1) {
        AMInput in;
        ioe_read(AM_INPUT_KEYBRD, &in);
        if (in.keydown && in.keycode == target_key) {
            fill_rect(x, y, BLOCK_SIZE, BLOCK_SIZE, 0x00000000);  // 消除
            target_key = (target_key == AM_KEY_Z) ? AM_KEY_A : target_key + 1;
            fill_rect(x, y, BLOCK_SIZE, BLOCK_SIZE, colors[target_key % 4]);
        }
    }
    return 0;
}
```

**2. 在 native 上测试逻辑**

先在 native 上编译运行，验证游戏流程（不验证画图，只验证按键逻辑打印正确）：

```bash
make -f Makefile.native   # native 平台
./typetest
```

**3. 编译 riscv32 版本**

```bash
make typetest.elf   # riscv32 平台，供联调3使用
```

### 检查点
- [ ] native 平台：按键后逻辑正确推进（即使画图只是打印信息）
- [ ] `typetest.elf` 用 riscv32 工具链编译通过（链接无 error）
- [ ] 用 `riscv32-unknown-elf-objdump -d typetest.elf` 查看入口在 0x80000000

---

## 阶段总结

完成本阶段后，你有了：

| 文件 | 内容 |
|------|------|
| `shal/include/am.h` | 完整 IOE 接口定义（两平台共用） |
| `shal/platform/native/ioe.c` | Native 实现（供 native 测试） |
| `shal/platform/riscv32/ioe.c` | riscv32 实现（供 SCore 运行） |
| `shal/apps/typetest/` | 打字游戏（联调3测试载体） |

**你现在能回答：**
- `ioe_read/write` 的抽象设计有什么好处？
- 为什么 MMIO 写操作要用 `volatile`？不加会有什么后果？
- Timer 为什么分两个寄存器读取，而不是一次读 64 位？
- `fbdraw_write` 的像素排列顺序是什么？为什么要按行写入？

---

## 进入下一步的前提

满足以下所有条件后，等待成员 A 完成 PA3-A，再一起进入「联调3」：

- [ ] 所有检查点通过
- [ ] `typetest.elf` 用 riscv32 编译通过
- [ ] 代码已推送到 `feat/pa3-shal-ioe` 分支

---

## 常见问题

**Q：riscv32 平台编译时报 "implicit declaration of REG"？**  
A：确认 `REG` 宏在 `.c` 文件顶部定义，或者放入某个头文件并 include。

**Q：native 平台的键盘读取在循环里一直返回 AM_KEY_NONE，即使我按了键？**  
A：终端默认是行缓冲模式，需要回车才发送。对于游戏来说，你需要把终端设置为原始模式（`cfmakeraw`），或者在 native 上就用这个测试绕过，只验证逻辑流程，不验证即时响应。

**Q：`fbdraw_write` 能调用但屏幕没有变化（riscv32 平台，联调3前）？**  
A：这是正常的——联调3之前 A 的 VGA 设备还没接上，或者 SCore 的 MMIO 写没有触发刷屏。暂时忽略，联调3时再排查。
