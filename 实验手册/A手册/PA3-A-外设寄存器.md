# 实验手册 · PA3-A：外设寄存器

> **本手册使用者：成员 A**  
> **阶段目标：** 在 SCore 中实现键盘、计时器、VGA 显示器的 MMIO 寄存器模拟，底层用 SDL2 驱动实际的窗口和键盘事件，让 B 的 SHAL 代码有真实的硬件可以交互。  
> **预计时间：** 3～4 天  
> **前置条件：** 联调2 完成（tag `v2.0-pa2`）；已知 B 的 SHAL 接口规范（两人在联调2时应已对齐各设备的 MMIO 地址）  
> **本阶段涉及文件：**
> ```
> score/
> ├── include/
> │   └── device.h        ← 本阶段新建（MMIO 地址常量，与 B 对齐）
> └── src/
>     ├── device/
>     │   ├── timer.cpp   ← 本阶段新建
>     │   ├── keyboard.cpp← 本阶段新建
>     │   └── vga.cpp     ← 本阶段新建
>     └── memory/
>         └── memory.cpp  ← 修改（扩展 MMIO 分发逻辑）
> ```
> **本阶段不涉及：** SHAL 的上层接口（由成员 B 负责）、操作系统（PA4）。

---

## 实验一：MMIO 分发框架

### 目标
把 PA2 中硬编码的 UART 处理，重构为通用 MMIO 分发机制，支持多个设备的注册与路由。

### 背景知识

**MMIO 地址空间规划：**

我们用以下地址布局（与成员 B 对齐，写入 `device.h`）：

```
0xa0000000  UART_TX           串口发送（PA2 已有）
0xa0001000  TIMER_LOW         计时器低32位（毫秒数的低位）
0xa0001004  TIMER_HIGH        计时器高32位
0xa0002000  KBD_STATUS        键盘状态（是否有按键）
0xa0002004  KBD_DATA          键盘数据（按键码）
0xa0003000  VGA_CTL           VGA 控制寄存器（宽高）
0xa0004000~ VGA_FRAMEBUFFER   像素数据（640*480*4 字节）
```

**MMIO 分发的设计：**

不要每增加一个设备就往 `paddr_write/read` 里添加一个 `if`。用注册表模式：

```cpp
struct MmioDevice {
    uint32_t  base;
    uint32_t  size;
    uint32_t  (*read)(uint32_t offset, int len);
    void      (*write)(uint32_t offset, int len, uint32_t data);
};
```

`paddr_read/write` 只需要遍历注册表，找到地址落在哪个设备范围内，调用对应设备的读写函数。

### 步骤

**1. 编写 `score/include/device.h`**

定义所有 MMIO 地址常量（这个文件两人共享，要和 B 对齐）：

```cpp
#pragma once
#include <cstdint>

#define UART_TX_ADDR    0xa0000000u
#define TIMER_BASE      0xa0001000u
#define KBD_BASE        0xa0002000u
#define VGA_CTL_BASE    0xa0003000u
#define VGA_FB_BASE     0xa0004000u

// VGA 分辨率
#define VGA_WIDTH   640
#define VGA_HEIGHT  480
```

**2. 重构 `memory.cpp`：建立 MMIO 注册表**

在 `memory.cpp` 中添加 MMIO 分发机制：

```cpp
// MMIO 注册表（最多16个设备）
struct MmioDevice { ... };  // 见上
static MmioDevice mmio_table[16];
static int        mmio_cnt = 0;

void mmio_register(uint32_t base, uint32_t size,
                   uint32_t (*rd)(uint32_t, int),
                   void     (*wr)(uint32_t, int, uint32_t)) {
    mmio_table[mmio_cnt++] = {base, size, rd, wr};
}

// 在 paddr_read/write 中，先查 MMIO 表
static bool try_mmio_read(uint32_t addr, int len, uint32_t *out) {
    for (int i = 0; i < mmio_cnt; i++) {
        if (addr >= mmio_table[i].base
         && addr < mmio_table[i].base + mmio_table[i].size) {
            *out = mmio_table[i].read(addr - mmio_table[i].base, len);
            return true;
        }
    }
    return false;
}
```

**3. 在 `main.cpp` 中初始化设备**

创建一个 `devices_init()` 函数（在 `device/` 各文件中实现），`main` 启动时调用，完成 SDL2 初始化和 MMIO 注册。

### 检查点
- [ ] MMIO 注册表结构编译通过
- [ ] UART 的功能改为通过注册表分发后仍正常工作（hello.elf 仍输出正确内容）

---

## 实验二：计时器（Timer）

### 目标
实现 `TIMER_LOW` 和 `TIMER_HIGH` 寄存器：读取这两个地址得到 SCore 启动以来的毫秒数。

### 背景知识

**为什么需要计时器？**  
游戏需要知道时间（控制帧率、动画速度）。操作系统需要计时器来产生定时中断（实现多任务调度）。

**实现思路：**  
读取 `TIMER_LOW` 时，用 `clock_gettime(CLOCK_MONOTONIC, ...)` 获取宿主机的单调时钟，计算从启动到现在的毫秒数，分成低32位和高32位两个寄存器返回。

为什么要分成两个32位寄存器而不是一个64位？因为我们的 CPU 是 32 位的，一次只能读32位。

**约定（与 B 对齐）：**
- 读 `TIMER_LOW` 时，同时计算并"锁存"高位到一个内部变量
- 读 `TIMER_HIGH` 时，返回上次读 `TIMER_LOW` 时锁存的高位值
- 这样两次读操作得到的是同一时刻的值，不会因时间流逝导致进位不一致

### 步骤

**1. 实现 `score/src/device/timer.cpp`**

```cpp
#include <ctime>
#include <cstdint>

static struct timespec boot_time;
static uint32_t        timer_high_latch = 0;

void timer_init() {
    clock_gettime(CLOCK_MONOTONIC, &boot_time);
    // 注册 MMIO
    mmio_register(TIMER_BASE, 8, timer_read, nullptr);
}

static uint32_t timer_read(uint32_t offset, int len) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t ms = (now.tv_sec - boot_time.tv_sec) * 1000
                + (now.tv_nsec - boot_time.tv_nsec) / 1000000;

    if (offset == 0) {
        timer_high_latch = (uint32_t)(ms >> 32);
        return (uint32_t)(ms & 0xffffffff);  // 读低32位
    } else {
        return timer_high_latch;  // 读高32位（返回锁存值）
    }
}
```

**2. 手动验证**

在 SDB 中执行：

```
(sdb) x 1 0xa0001000    # 读 TIMER_LOW，应得到一个非零毫秒数
(sdb) x 1 0xa0001000    # 再读一次，值应该更大了
```

### 检查点
- [ ] 两次读 `0xa0001000` 的值递增
- [ ] 读 `0xa0001004` 返回上次读 `0xa0001000` 时锁存的高位（通常为0，除非 SCore 运行超过 49 天）

---

## 实验三：键盘（Keyboard）

### 目标
用 SDL2 捕获键盘事件，实现 `KBD_STATUS` 和 `KBD_DATA` 寄存器。

### 背景知识

**SDL2 事件系统：**  
SDL2 的事件系统是异步的：用户按键后，SDL 把事件放入队列。程序调用 `SDL_PollEvent` 取出队列头部的事件。

键盘事件有两种：`SDL_KEYDOWN`（按下）和 `SDL_KEYUP`（松开）。每个键有一个 `SDL_Keycode`（SDL 定义的键码，如 `SDLK_a`、`SDLK_LEFT`）。

**寄存器语义（与 B 对齐）：**
- `KBD_STATUS`（偏移 0，只读）：如果有键盘事件待处理，返回 1；否则返回 0
- `KBD_DATA`（偏移 4，只读）：读取一个键盘事件，编码格式如下：

```cpp
// 键盘数据编码（与 B 的 SHAL 约定）
// bit31: 1=按下, 0=松开
// bit[30:0]: 按键码（用 AM 定义的键码，不是 SDL 键码）
uint32_t encode_key(bool is_down, int amkey) {
    return ((uint32_t)is_down << 31) | (uint32_t)amkey;
}
```

> 为什么不直接用 SDL 键码？因为 B 的 SHAL 层需要一套与平台无关的键码（AM 键码），你在这里做转换，上层就不需要关心 SDL 细节了。

**AM 键码定义（两人共同定义在 `device.h` 中）：**

```cpp
enum AMKey {
    AM_KEY_NONE = 0,
    AM_KEY_ESCAPE,
    AM_KEY_RETURN,
    AM_KEY_BACKSPACE,
    AM_KEY_SPACE,
    AM_KEY_A, AM_KEY_B, ..., AM_KEY_Z,
    AM_KEY_0, ..., AM_KEY_9,
    AM_KEY_F1, ..., AM_KEY_F12,
    AM_KEY_UP, AM_KEY_DOWN, AM_KEY_LEFT, AM_KEY_RIGHT,
    // ... 根据需要扩充
};
```

### 步骤

**1. 初始化 SDL2**

在 `timer_init` 或单独的 `sdl_init` 中初始化 SDL2：

```cpp
void sdl_init() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    // 窗口和渲染器在 VGA 模块初始化（实验四中）
}
```

**2. 实现 `score/src/device/keyboard.cpp`**

- 维护一个小的循环队列（或者直接用 SDL 的事件队列）
- `kbd_read(offset, len)`：
  - offset == 0：调用 `SDL_PollEvent`，若有键盘事件返回 1，否则返回 0
  - offset == 4：调用 `SDL_PollEvent` 取出事件，返回 `encode_key(...)` 的编码
- 需要一个 SDL → AM 键码的映射表（switch-case 或者数组）

**3. 注册到 MMIO 表**

```cpp
void keyboard_init() {
    mmio_register(KBD_BASE, 8, kbd_read, nullptr);
}
```

### 检查点
- [ ] 在 SDB 中：运行到一个 `x 1 0xa0002000` 循环里，按下键盘后返回值从 0 变为 1
- [ ] `x 1 0xa0002004` 能读出正确的按键编码（先读 STATUS 再读 DATA）

---

## 实验四：VGA 显示器

### 目标
用 SDL2 创建窗口，实现 VGA Framebuffer 的 MMIO 写入：程序向 `VGA_FB_BASE` 开始的内存写像素，SCore 把这些像素渲染到 SDL 窗口上。

### 背景知识

**Framebuffer 的工作方式：**  
VGA Framebuffer 是一段连续的内存，每个像素占 4 字节（ARGB 格式，A 通道忽略）。程序直接写像素值到这段内存，显示控制器（这里是 SDL2）从中读取并刷新屏幕。

分辨率 640×480，每帧像素数据大小 = 640 × 480 × 4 = 1,228,800 字节（约 1.2MB）。

**SDL2 渲染流程：**  
SDL2 的标准渲染模式：`SDL_Texture` 作为画布，用 `SDL_UpdateTexture` 把像素数据复制进去，然后 `SDL_RenderCopy + SDL_RenderPresent` 显示到窗口。

**刷新时机：**  
不需要每次像素写入都刷新（太慢），在 SCore 的主循环（`cpu_exec`）每执行一批指令后刷新一次即可。

### 步骤

**1. 在 `score/src/device/vga.cpp` 中维护一个 Framebuffer 缓冲区**

```cpp
static uint32_t fb[VGA_WIDTH * VGA_HEIGHT];  // 像素缓冲，宿主机侧
static SDL_Window   *window   = nullptr;
static SDL_Renderer *renderer = nullptr;
static SDL_Texture  *texture  = nullptr;
```

**2. 实现 `vga_init()`**

```cpp
void vga_init() {
    window   = SDL_CreateWindow("Stratum", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                VGA_WIDTH, VGA_HEIGHT, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 VGA_WIDTH, VGA_HEIGHT);
    // 注册 MMIO：VGA 控制寄存器（只读）
    mmio_register(VGA_CTL_BASE, 8, vga_ctl_read, nullptr);
    // 注册 Framebuffer（只写）
    mmio_register(VGA_FB_BASE, VGA_WIDTH * VGA_HEIGHT * 4, nullptr, vga_fb_write);
}
```

**3. 实现 Framebuffer 写入**

```cpp
static void vga_fb_write(uint32_t offset, int len, uint32_t data) {
    // offset 以字节为单位，像素数组以4字节为单位
    int pixel_idx = offset / 4;
    if (pixel_idx >= VGA_WIDTH * VGA_HEIGHT) return;
    fb[pixel_idx] = data;
}
```

**4. 实现屏幕刷新函数**

```cpp
void vga_update_screen() {
    SDL_UpdateTexture(texture, nullptr, fb, VGA_WIDTH * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}
```

这个函数由 `cpu_exec` 每隔一定步数调用，比如每执行 10000 条指令后调用一次。

**5. 实现 VGA 控制寄存器读取（供 B 查询分辨率）**

```cpp
static uint32_t vga_ctl_read(uint32_t offset, int len) {
    if (offset == 0) return VGA_WIDTH;
    if (offset == 4) return VGA_HEIGHT;
    return 0;
}
```

### 检查点
- [ ] `vga_init()` 后出现一个 640×480 的黑色窗口
- [ ] 向 `VGA_FB_BASE` 写白色像素 `0x00ffffff`，调用 `vga_update_screen()` 后窗口出现白色
- [ ] SDB 下 `x 1 0xa0003000` 返回 640，`x 1 0xa0003004` 返回 480

---

## 阶段总结

完成本阶段后，SCore 拥有了：

| 设备 | MMIO 地址 | 功能 |
|------|----------|------|
| UART | 0xa0000000 | 串口输出（PA2 已有） |
| Timer | 0xa0001000 | 毫秒计时 |
| Keyboard | 0xa0002000 | 键盘按键事件 |
| VGA | 0xa0003000 / 0xa0004000 | 分辨率查询 / 像素写入 |

**你现在能回答：**
- MMIO 和普通内存访问有什么区别？为什么要在 `check_addr` 之前处理 MMIO？
- SDL2 的事件系统是怎么工作的？`SDL_PollEvent` 和阻塞等待有什么区别？
- VGA 的 Framebuffer 是怎么工作的？为什么不需要每次写像素都刷新屏幕？
- 为什么不直接把 SDL 键码暴露给上层，而是定义一套 AM 键码？

---

## 进入下一步的前提

满足以下所有条件后，等待成员 B 完成 PA3-B，再一起进入「联调3」：

- [ ] 所有检查点通过
- [ ] `device.h` 中的 MMIO 地址常量已提交，B 可以使用
- [ ] 代码已推送到 `feat/pa3-devices` 分支

---

## 常见问题

**Q：SDL2 窗口一闪而过就消失了？**  
A：`SDL_RenderPresent` 之后没有保持事件循环。你需要在某个地方定期调用 `SDL_PollEvent`（即使不处理键盘事件，也要 poll 一下让 SDL 有机会处理窗口消息）。把 `vga_update_screen` 中的 event poll 加进去。

**Q：编译报 "SDL.h: No such file or directory"？**  
A：SDL2 开发包未安装，或者 Makefile 中没有加 `$(shell sdl2-config --cflags)`。用 `sdl2-config --cflags --libs` 获取正确的编译和链接参数。

**Q：VGA 显示器什么都没有，但也没报错？**  
A：检查 `vga_update_screen` 有没有被调用。可以在 SDB 里加一个 `screen` 命令手动触发刷新，调试时用。
