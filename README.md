# STM32 全自动洗衣机课程仿真

当前版本：**v2.2.0 · 2026-11-26**。用于嵌入式原理与应用课程展示，基于 STM32F103C8、FreeRTOS 和 Proteus 8.17。

主分支包含 v2.2.0 之后的翻页响应更新（`2026-11-26-page-response`）；原 v2.2.0 标签保留原始版本。**已知问题：Proteus 手动翻页按钮仍有回弹卡顿反馈，尚未确认修复。** 已排查按钮参数与 OLED 传输负载，详见 [翻页回弹原因排查](Validation/翻页回弹原因排查.txt)。当前 HEX 保留正常中文显示，不含临时停用像素传输的诊断改动。

## 直接运行

1. 克隆或下载完整仓库，保留目录结构。
2. 使用 Proteus 8.17 打开根目录 `Washer.pdsprj`，按 F12 运行。工程通过相对路径加载同目录 `WashingMachine.hex`。
3. 确认门开关闭合，使用“模式选择”“启动/暂停”“停止/自检”操作。
4. “手动翻页”循环切换流程总览、参数、执行器状态。按钮按下后需释放，再进行下一次操作。

无需重新编译即可仿真。图形显示采用 Proteus 自带的 OLED12864I2C，中文字模随源码提供。

## 功能

- AUTO、QUICK、STD、HEAVY、WOOL 五种程序；自动配方根据重量与污浊度选择。
- 锁门、进水、加热、正反转洗涤、排水、漂洗、脱水、停机、解锁。
- 暂停继续、急停、开门保护、进排水超时、超温、不平衡重试、停转超时。
- 中文 OLED 四行显示，手动翻页、自动翻页、故障优先提示。
- 串口控制、周期遥测、故障注入、Flash 日志回读、六任务诊断。
- 显示文本缓存与变化列刷新；100 ms 显示检查；课堂仿真 PWM 为 1 kHz。

水量、温度、转速等过程量采用软件对象模型。`h` 是超温故障码注入，不会改变温度数值。该工程用于课程仿真，未作实机系统验证。

## 文件结构

```text
Washer.pdsprj         Proteus 电路入口
WashingMachine.hex   与当前源码对应的可运行固件
Firmware/            完整 Keil 工程、应用、芯片库和 FreeRTOS
Docs/                中文使用说明和验证记录
Docs/历史说明/        前版说明，当前接线与显示以中文屏幕说明为准
Validation/          控制模型检查、显示检查与字模生成脚本
VERSION.json         版本及固件 SHA256
CHANGELOG.md         更新记录
SHA256SUMS.txt       交付文件校验清单
```

详细操作见 [中文屏幕版使用说明](Docs/中文屏幕版使用说明.txt)。接线：OLED 的 SCL/SDA 分别使用 PB0/PB1，PA3 为翻页按钮；完整引脚定义见 `Firmware/Application/app_config.h`。

## 编译

打开 `Firmware/Washer_Demo.uvprojx`，选择 ARM Compiler 5.06 update 5，安装工程声明的 `Keil.STM32F1xx_DFP 2.2.0`，执行 Build/Rebuild。

工程使用 8 MHz 时钟。编译后自动将 `Firmware/Objects/WashingMachine.hex` 复制到仓库根目录，供 Proteus 加载。`Objects`、`Listings` 和个人窗口配置不提交。

## 串口命令

Debug 菜单打开 Virtual Terminal，使用英文输入法，115200、8N1，逐字输入，无需回车。

| 命令 | 功能 |
| --- | --- |
| `?` / `g` | 帮助 / 当前状态操作提示 |
| `m` / `s` | 待机选程序 / 启动、暂停、继续、复位 |
| `x` / `t` | 急停 / 自检 |
| `p` / `v` | 状态查询 / 每秒遥测开关 |
| `r` / `l` | 任务、内存诊断 / Flash 日志 |
| `i` / `d` / `b` | 进水堵塞 / 排水堵塞 / 不平衡开关 |
| `h` / `c` | 超温故障码 / 清除三种模型注入条件 |
| `0` / `1` / `2` / `a` | 固定显示页 / 自动翻页 |

## 验证

Keil 构建为 0 错误、0 警告；本机 Proteus 验证覆盖中文显示、翻页、快洗完成、急停及复位。详细验证范围见 `Docs` 和 `Validation` 的记录，未穷举全部程序与故障组合。

已有 Zig C 工具链时，在 PowerShell 执行：

```powershell
.\Validation\Run-ModelChecks.ps1 -ZigPath 'C:\工具链\zig.exe'
.\Validation\Run-DisplayChecks.ps1 -ZigPath 'C:\工具链\zig.exe'
.\Validation\Run-PageKeyChecks.ps1 -ZigPath 'C:\工具链\zig.exe'
.\Validation\Run-DisplayLoadCheck.ps1 -ZigPath 'C:\工具链\zig.exe'
```

模型检查覆盖快速程序完成、进排水超时、不平衡重试、暂停继续、超温分支、自检恢复；显示检查从当前驱动提取真实函数，验证缓存和局部更新。主机检查不覆盖 MCU 外设或 FreeRTOS 调度。

按键检查验证中断屏蔽、首次按下通知及释放消抖；负载脚本比较实际驱动的像素发送量。以上检查不能代替 Proteus 中鼠标松开至图形回弹的实际时间验证。Validation 中各阶段记录保留当时测试状态，最新结论以翻页回弹排查记录为准。

如需新增汉字，可使用 Python + Pillow 运行 `Validation/Generate-OledFont.py`；该脚本需要 Windows 宋体和 Consolas，生成的字模已包含在工程中，正常编译不需要 Python。

## 和朋友一起修改

1. 每次修改前先 `git pull --ff-only`。
2. 新建功能分支，例如 `git switch -c feature/new-program`。
3. 修改源码或电路，编译并完成相应验证；固件有变化时，一并提交根目录 HEX。
4. 更新 `CHANGELOG.md`、`VERSION.json` 及文件校验清单；提交后推送分支，通过 Pull Request 合并。

尽量避免两人同时修改二进制 `Washer.pdsprj`，电路修改可约定由一人完成。保留芯片库和 FreeRTOS 源文件中的原始版权与许可证。
