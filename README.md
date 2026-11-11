# Smart-Car

基于 STM32F103C8T6 的四轮两驱循迹小车工程。目标主链路为：

```text
LF04 四路红外
  -> 线路语义
  -> 循迹策略
  -> 左右轮目标速度
  -> 双轮速度闭环
  -> 应用安全门
  -> TB6612FNG
  -> MG310P20 左右驱动轮
```

MPU6050、SSD1306 OLED、HC-05 和 CH340 属于辅助/调试通道。完整硬件清单、接线、软件分层和验收矩阵见[硬件接线与软件 API 规划](docs/循迹小车硬件接线与软件API规划.md)。

## 当前状态

截至 2026-11-12，基础外设驱动和非阻塞服务层已经完成，正式固件可以启动并持续采集核心执行器与传感器状态，但尚未实现循迹控制、双轮速度闭环和比赛应用层。

| 模块 | 当前状态 |
|---|---|
| 时钟、GPIO、I2C2、TIM2/TIM3/TIM4、USART1/USART2 | CubeMX/HAL 初始化完成 |
| `sc_uart` | USART1/2 非阻塞收发、环形缓冲、错误与溢出统计完成 |
| `sc_i2c2`、`sc_mpu6050`、`sc_ssd1306` | 共享 I2C2 事务、MPU6050 异步采样/校零、OLED 分块刷新完成 |
| `sc_tb6612`、`sc_encoder` | 双路 PWM、方向、安全待机、TIM2/TIM3 正交计数、回绕与失同步处理完成 |
| `sc_lf04`、`sc_line_service` | PB12-PB15 原始采样、黑线掩码和 16 种线路语义完成 |
| `sc_button`、`sc_led` | 基础输入轮询与状态 LED 驱动完成 |
| `sc_bt_bench` | 独立 Bench 固件的 HC-05 台架命令层完成；正式固件不链接 |
| Wheel Service、速度环/PID、循迹策略、应用安全门 | 尚未实现 |

正式 Debug/Release 固件保持静默，不接受蓝牙运动命令。电机台架控制仅存在于独立 `Bench` 固件。

## 硬件基线

- 主控：STM32F103C8T6 核心板，72 MHz。
- 驱动与执行器：单块 TB6612FNG、两台带霍尔编码器的 MG310P20 直流减速电机。
- 底盘：后端两轮驱动，前端两个万向轮。
- 循迹输入：LF04 四路红外，`O1..O4` 对应 `PB12..PB15`，预期白底为 `1`、黑线为 `0`。
- 编码器：左右霍尔 A/B 分别接 TIM2 和 TIM3 正交接口。
- 辅助设备：MPU6050、SSD1306 OLED、HC-05；CH340G 用于有线台架串口调试。

## 构建

需要 CMake 3.22 或更高版本、Ninja 和 Arm GNU Toolchain（`arm-none-eabi-gcc`）。

```powershell
# 正式调试固件
cmake --preset Debug
cmake --build --preset Debug

# 正式发布固件
cmake --preset Release
cmake --build --preset Release

# HC-05 台架固件
cmake --preset Bench
cmake --build --preset Bench
```

产物：

| 预设 | ELF |
|---|---|
| `Debug` | `build/Debug/Smart-Car.elf` |
| `Release` | `build/Release/Smart-Car.elf` |
| `Bench` | `build/Bench/Smart-Car-HC05Bench.elf` |

各构建目录同时生成 `Smart-Car.map`。

## 蓝牙 Bench

Bench 固件通过 USART2 与 HC-05 通信，串口参数为 `9600 8N1`。支持的主要命令：

- `PING`：链路测试。
- `STATUS`：TB6612 和编码器状态。
- `LINE` / `LF`：LF04 原始电平、掩码和线路语义。
- `ARM`：打开 5 秒布防窗口。
- `MOTOR <L|R> <±150> <100-2000>`：单轮受限测试。
- `STOP`：立即零输出并撤销 STBY。
- `RESET`：停止状态下重置编码器计数。
- `HELP`：命令列表。

Windows 工具示例：

```powershell
pwsh .\tools\sc_bt_bench.ps1 -Port COM13 -Action Probe
pwsh .\tools\sc_bt_bench.ps1 -Port COM13 -Action Line
pwsh .\tools\sc_bt_bench.ps1 -Port COM13 -Action DirectionMatrix -ExecuteMotion
```

实际 COM 口以设备管理器为准。`DirectionMatrix` 会驱动真实电机，只能在双轮悬空、电池和 ST-Link 已互斥连接时使用。

## 验证状态

已完成的代码级验证：

- UART 主机回归：10/10。
- I2C2/MPU6050/SSD1306：15/15，缺陷变体 13/13。
- TB6612/编码器：23/23，缺陷变体 17/17。
- LF04/线路语义：13/13，缺陷变体 11/11。
- HC-05 Bench：31/31，缺陷变体 13/13。
- 新模块通过主机 GCC `-fanalyzer`。
- Debug、Release、Bench 均完成交叉编译和链接。
- 正式 ELF 不含 `sc_bt_bench` 符号或 Bench 协议字符串，HAL 回调无重复。

已完成的硬件台架项：

- TB6612 左右通道隔离、单轮短脉冲、自动停机和霍尔计数已验证。
- 右电机方向接反已通过交换动力线纠正；后续 Wheel Service 需采用左编码器 `-1`、右编码器 `+1` 的名义方向映射。
- HC-05 SPP 已通过 `COM13 @ 9600 8N1` 完成命令交互。

尚未验证：

- LF04 当前未接线，白底/黑线极性、逐路响应、约 3 cm 安装高度和 O1-O4 左右顺序未验证。
- 双轮速度闭环、实际车速、轮径/每转计数标定和循迹策略未验证。
- 堵转电流、温升、长时间运行、满电负载供电和完整赛道表现未验证。

## 安全约束

- ST-Link、电池和 Type-C 不得同时向目标板供电。
- 烧录前必须物理断开电池；烧录完成后移除 ST-Link，再接入电池运行。
- 电机动力线不得带电热插拔。
- 首次运动测试必须使驱动轮悬空，并保持电池开关随手可断。
- Bench 运动命令只有限时、单轮、低占空比控制，不能替代应用安全门和速度闭环。

## 测试

主机测试项目位于 `tests/`，每个模块使用独立假 HAL 和 CTest 用例。

```powershell
cmake -S tests/sc_line -B build/host-sc-line -G Ninja `
  -DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe
cmake --build build/host-sc-line
ctest --test-dir build/host-sc-line --output-on-failure
```

其余回归目录为 `tests/sc_uart`、`tests/sc_i2c2`、`tests/sc_motor` 和 `tests/sc_bt_bench`。缺陷对照脚本位于对应测试目录的 `run_mutant_tests.ps1`。

## 后续工作

1. 接线并完成 LF04 白底、全黑、逐路黑线和左右顺序标定。
2. 实现 Wheel Service，应用左右编码器方向、1040 count/rev 和 48 mm 轮径。
3. 实现双轮速度闭环及低速 PID 标定。
4. 在已校准的线路语义和轮速反馈上实现循迹策略与应用安全门。
5. 完成整车供电、长时间运行、温升和比赛赛道测试。
