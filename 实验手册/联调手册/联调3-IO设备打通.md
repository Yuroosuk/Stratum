# 实验手册 · 联调3：IO 设备打通

> **参与者：双人共同完成**  
> **阶段目标：** 在 SCore 的 SDL2 窗口中运行 `typetest.elf`，玩家能看到彩色方块并通过键盘交互，Timer/Keyboard/VGA 三个设备全部验证通过。  
> **预计时间：** 半天～1天  
> **前置条件：** PA3-A 所有检查点通过；PA3-B 所有检查点通过

---

## 实验一：合并代码

### 步骤

```bash
git checkout dev
git pull origin dev
git merge --no-ff feat/pa3-devices    -m "merge: pa3-a MMIO devices"
git merge --no-ff feat/pa3-shal-ioe   -m "merge: pa3-b SHAL IOE"
make clean && make
```

**检查合并后编译：**
- [ ] `score/` 编译通过（含 SDL2）
- [ ] `shal/apps/typetest/` riscv32 版本编译通过

---

## 实验二：逐设备验证

### 目标
按设备顺序逐一验证，出问题时范围已经收窄。

### 步骤

**设备1：Timer**

用 SDB 手动读取计时器寄存器：

```
./score/score shal/apps/hello/hello.elf
(sdb) x 1 0xa0001000    # 第一次读，记下值 V1
(sdb) x 1 0xa0001000    # 第二次读，记下值 V2
```

V2 应该 > V1，差值约等于两次命令之间的毫秒数。

**期望：** V2 > V1。

**设备2：Keyboard**

先让 SCore 进入一个读键盘的循环（用 si 慢慢运行，或者直接加载一个简单汇编程序），在 SDL2 窗口激活的情况下按一个键，然后在 SDB 中：

```
(sdb) x 1 0xa0002000   # KBD_STATUS
(sdb) x 1 0xa0002004   # KBD_DATA
```

先读 STATUS，若有事件返回 1；再读 DATA，取出键盘编码。

**设备3：VGA**

写一个最小的测试：向 Framebuffer 写几个像素，然后触发刷屏：

```c
// 临时在 hello.elf 的 main 里加入：
*(volatile uint32_t *)0xa0004000 = 0x00ff0000;  // 左上角第一个像素改为红色
```

如果 VGA 模块正确，SDL 窗口左上角应该出现一个红色像素。

---

## 实验三：运行 typetest.elf

### 步骤

**1. 编译 typetest.elf**

```bash
cd shal/apps/typetest
make typetest.elf
```

**2. 运行**

```bash
./score/score shal/apps/typetest/typetest.elf -b
```

**3. 逐步排查（如果没有预期效果）**

| 症状 | 排查方向 |
|------|---------|
| SDL 窗口不出现 | SCore 的 `vga_init()` 有没有被调用？检查 `main.cpp` 中 `devices_init()` 的调用 |
| 窗口是黑的，没有方块 | `fbdraw_write` 有没有被调用？在 `vga_fb_write` 里加 `printf` 调试 |
| 方块出现了但按键没有反应 | SDL 窗口必须获得焦点（用鼠标点击一下窗口），然后再按键 |
| 按键有反应但方块没有更新 | `vga_update_screen()` 有没有在 CPU 主循环中定期调用？ |
| SCore abort，非法地址 | 检查 `fbdraw_write` 计算的 offset 是否越界（`VGA_FB_BASE + offset` 超出 MMIO 注册的范围） |

**4. 确认通过的标志**

- SDL 窗口内出现彩色方块
- 按 A/B/C... 对应方块消失，下一个出现
- 程序持续运行，不崩溃，不 abort

---

## 对齐检查：两人确认接口一致

完成设备测试后，两人对照以下清单，确认所有接口定义一致：

| 项目 | A 侧（score/device.h） | B 侧（shal/platform/riscv32/ioe.c） | 是否一致 |
|------|----------------------|-------------------------------------|---------|
| UART TX 地址 | 0xa0000000 | 0xa0000000 | ☐ |
| TIMER_LOW 地址 | 0xa0001000 | 0xa0001000 | ☐ |
| TIMER_HIGH 地址 | 0xa0001004 | 0xa0001004 | ☐ |
| KBD_STATUS 地址 | 0xa0002000 | 0xa0002000 | ☐ |
| KBD_DATA 地址 | 0xa0002004 | 0xa0002004 | ☐ |
| VGA 宽高寄存器 | 0xa0003000/04 | 0xa0003000/04 | ☐ |
| VGA FB 起始地址 | 0xa0004000 | 0xa0004000 | ☐ |
| 像素格式 | ARGB 0x00RRGGBB | ARGB 0x00RRGGBB | ☐ |
| 键盘数据 bit31 | 1=按下 | 1=按下 | ☐ |
| AM_KEY_A 值 | 5（举例） | 5（举例） | ☐ |

---

## 提交与打 tag

```bash
make clean && make

# PR dev → main，Review 通过后
git checkout main
git pull origin main
git tag v3.0-pa3
git push origin v3.0-pa3
```

## 联调完成检查清单

- [ ] Timer 计时寄存器值递增
- [ ] Keyboard 能读到正确的按键编码
- [ ] VGA 能在窗口中显示颜色
- [ ] `typetest.elf` 在 SCore 上可交互运行
- [ ] MMIO 地址表双方完全一致（上方对齐表全部勾选）
- [ ] tag `v3.0-pa3` 已推送

---

*联调3 完成 → 成员 A 进入「PA4-A 中断与陷入」，成员 B 进入「PA4-B SOS操作系统」，实现最终的多任务操作系统。*
