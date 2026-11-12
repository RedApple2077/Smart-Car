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

MPU6050、SSD1306 OLED 和 HC-05 属于可选辅助/遥测通道。完整硬件清单、接线、软件分层和验收矩阵见[硬件接线与软件 API 规划](docs/循迹小车硬件接线与软件API规划.md)。

## 当前状态

截至 2026-11-13，正式闭环主链已接入 `sc_app`：

```text
10 ms LF04 -> 线路语义 -> 循迹 mm/s 目标
  -> 10 ms Wheel Service 滑窗反馈 -> 双轮 PID
  -> sc_app 安全提交 -> TB6612
```

正式 Debug/Release 构建只通过 `sc_app` 提交 TB6612 的 PWM 与 STBY，HC-05 入站接收关闭；Bench 固件保留原有台架命令，并同样启用 IWDG。默认标定和 PID 仍是未验证占位，`SC_MOTOR_CALIBRATION_VALID=0`、`SC_MOTOR_TUNING_VALID=0`，因此正式固件上电固定停在 IDLE，不会因默认参数自动发车。完整实车边界见[循迹调试日志](docs/循迹调试日志.md)，控制与实验状态见[实现与实验记录](docs/双轮速度控制实现与实验记录.md)。

| 模块 | 当前状态 |
|---|---|
| 时钟、GPIO、I2C2、TIM2/TIM3/TIM4、USART2 | CubeMX/HAL 初始化完成 |
| `sc_uart` | HC-05/USART2 9600 8N1 非阻塞收发、可选 RX、环形缓冲、错误与溢出统计完成 |
| `sc_button` | 上电稳定态、事件配对、消抖与长按完成 |
| `sc_safety` | 寄存器级紧急停机；IWDG 先启动 LSI，再有界等待配置更新；`/64`、重装 312、名义 0.4992 s |
| `sc_i2c2`、`sc_mpu6050`、`sc_ssd1306` | 共享 I2C2 事务、MPU6050 异步采样/校零、OLED 分块刷新完成 |
| `sc_tb6612`、`sc_encoder` | 双路 PWM、方向、安全待机、TIM2/TIM3 正交计数、回绕与失同步处理完成 |
| `sc_lf04`、`sc_line_service` | PB12-PB15 原始采样、黑线掩码和 16 种线路语义完成 |
| `sc_led` | 状态 LED 驱动完成 |
| `sc_bt_bench` | 独立 Bench 固件的 HC-05 台架命令层完成；正式固件不链接 |
| `sc_track` | 四路比例策略输出 `mm/s`、发车资格、1000 ms 方向找回、三帧/20 ms 重捕完成 |
| `sc_wheel_service`、`sc_pid`、`sc_speed_controller` | 约 50 ms 滑窗测速、并联 PID 与双轮速度闭环已接入正式主循环 |
| `sc_app` | BOOT/IDLE/READY/ACTIVE/STOPPING/FAULT、启动门、故障恢复、堵转/方向保护和唯一执行器提交完成 |
| `sc_telemetry` | HC-05 `9600 8N1`、5 Hz 紧凑 CSV、2 Hz OLED、状态 LED 完成 |

正式 Debug/Release 固件只发送只读遥测，不启用入站接收，也不包含蓝牙运动命令。电机台架控制仅存在于独立 `Bench` 固件。

## 硬件基线

- 主控：STM32F103C8T6 核心板，72 MHz。
- 驱动与执行器：单块 TB6612FNG、两台带霍尔编码器的 MG310P20 直流减速电机。
- 底盘：后端两轮驱动，前端两个万向轮。
- 循迹输入：LF04 四路红外，`O1..O4` 对应 `PB12..PB15`，预期白底为 `1`、黑线为 `0`。
- 编码器：左右霍尔 A/B 分别接 TIM2 和 TIM3 正交接口。
- 辅助设备：MPU6050、SSD1306 OLED、HC-05；不再保留额外有线 USB 串口调试器。

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
- `RAW <L|R|B> <pwm> <100-2000>`：开环辨识，`abs(pwm)<=564`，必须 ARM；B 两侧同号输出；到期自动停止。
- `SPEED <L|R|B> <mm_s> <100-2000>`：闭环速度实验，`abs(mm_s)<=500`，必须 ARM 且 Wheel 反馈有效；B 两侧同目标，单侧命令另一侧目标为 0。
- `PID <L|R|B> <kp_milli> <ki_milli> <kd_milli>`：仅停止且未布防时修改 RAM 增益；范围 `±10000`，保留各侧 ff。
- `CAPTURE`：停轮后导出最近一次 MOTOR/RAW/SPEED 的 100 Hz 样本；先发送 `OK CAPTURE BEGIN N=<count>`，再分块发送 CAP 行和 `OK CAPTURE END`。
- `STOP`：立即零输出并撤销 STBY。
- `RESET`：停止状态下重置编码器计数。
- `HELP`：命令列表。

Windows 工具示例：

```powershell
pwsh .\tools\sc_bt_bench.ps1 -Port COM13 -Action Probe
pwsh .\tools\sc_bt_bench.ps1 -Port COM13 -Action DirectionMatrix -ExecuteMotion
pwsh .\tools\sc_hc05_tune.ps1 -Port COM13 -Action Identify
pwsh .\tools\sc_hc05_tune.ps1 -Port COM13 -Action SetPid -Wheel B -KpMilli 100 -KiMilli 10 -KdMilli 0
pwsh .\tools\sc_hc05_tune.ps1 -Port COM13 -Action Step -Wheel B -TargetMmS 200
```

`sc_hc05_tune.ps1` 的所有动作都要求：Windows 能通过 COM 设备 DeviceID 读取 ContainerId，且同一 PnP Container 中存在名称匹配 `HC-05` 的设备。ContainerId 不可查、同容器无 HC-05、CH340 或普通 USB 串口均拒绝；不再凭通用 Bluetooth 名称放行。CAPTURE 导出窗口统一至少 30s，覆盖 201 行约 80 B/行在 9600 8N1 下的约 17s 传输。工具先解析 BEGIN 的 expected count，再核对 CAP 行数精确相等、时间戳严格递增、模式一致及 Identify/Step 的时长覆盖和非零 fault；不完整或故障数据保留原始日志/CSV 后抛错，不产生速度、ff 或 PID 性能结论。Identify 任一轮左右速度为非正，或左右三轮相对离散度任一超过 10% 时，只保留原始结果并给出 warning，不输出 cruise/ff 建议。日志只写到 `build/tune-logs`。实际 COM 口以设备管理器为准。所有运动命令都会驱动真实电机，只能在双轮悬空、电池和 ST-Link 已互斥连接时使用。PID 命令只改掉电即丢失的 RAM 参数，不修改源码或两个 VALID 标志。

## 验证状态

已完成的代码级验证：

- UART 主机回归：13/13，缺陷变体 3/3。
- 按键主机回归：3/3，缺陷变体 1/1。
- Safety：3/3，缺陷变体 1/1；覆盖紧急停机和 IWDG 更新超时。
- Wheel Service：8/8，缺陷变体 6/6。
- PID：6/6，缺陷变体 4/4。
- 双轮速度控制：10/10，缺陷变体 7/7。
- 循迹目标策略：13/13，缺陷变体 7/7。
- I2C2/MPU6050/SSD1306：15/15，缺陷变体 13/13。
- TB6612/编码器：24/24，缺陷变体 18/18。
- LF04/线路语义：13/13，缺陷变体 11/11。
- HC-05 Bench：50/50，缺陷变体 28/28。
- App：24/24，缺陷变体 10/10。
- Telemetry：8/8，缺陷变体 5/5。
- 全量 13 个主机测试套件共 190/190 个基线测试。
- Speed Controller、Bench、App、Track、Telemetry、Safety 通过主机 GCC `-fanalyzer`，无分析告警。
- Debug、Release、Bench 均通过交叉编译。

最终资源占用：

| 预设 | RAM | Flash | 相对上一版 |
|---|---:|---:|---|
| Debug | `5192 B` | `58804 B` | `0 B` / `0 B` |
| Release | `5184 B` | `31424 B` | `0 B` / `0 B` |
| Bench | `12552 B` | `62208 B` | `+72 B` / `+2600 B` |

资源差值为第四阶段相对上一版的变化。Release ELF 含 `sc_app_*`、`sc_telemetry_*`、`sc_safety_iwdg_*`，不含 CAPTURE 缓冲、RX 命令解析、`sc_bt_bench_*` 或 `RAW/SPEED/PID/CAPTURE` 命令字符串；Bench ELF 保留完整调参命令与捕获缓冲。

已完成的硬件台架项：

- TB6612 左右通道隔离、单轮短脉冲、自动停机和霍尔计数已验证。
- 右电机方向接反已通过交换动力线纠正；后续 Wheel Service 需采用左编码器 `-1`、右编码器 `+1` 的名义方向映射。
- HC-05 SPP 已通过 `COM13 @ 9600 8N1` 完成命令交互。

尚未验证：

- LF04 白底/黑线极性、逐路响应、O1-O4 左右顺序、中心直行与十字直行已完成实物验证；探头约 3 cm 安装高度与完整赛道仍需复测。
- 双轮速度闭环代码已接入，但实际车速、轮径/每转计数、电机符号和 PID 参数仍未实车标定；默认有效位保持 0。
- 堵转电流、温升、长时间运行、满电负载供电和完整赛道表现未验证。

## 安全约束

- ST-Link、电池和 Type-C 不得同时向目标板供电。
- 烧录前必须物理断开电池；烧录完成后移除 ST-Link，再接入电池运行。
- 电机动力线不得带电热插拔。
- 首次运动测试必须使驱动轮悬空，并保持电池开关随手可断。
- Bench 运动命令有 ARM、100..2000 ms 限时、RAW `±564`、SPEED `±500 mm/s` 和 STBY 安全停机；RAW/SPEED 均要求双轮 feedback 有效且样本不超过 30 ms，并支持单侧或双轮 B。运行中任一侧 invalid、lost_sync 或样本年龄超过 30 ms时立即记录 FEEDBACK fault、保存 CAPTURE、拉低 STBY 并返回 `ERR FEEDBACK`。`|实际TB命令|>=150` 时执行 300 ms 滚动净进展检查：RAW 至少净增 20 个物理计数，这是尚未经过台架标定的保守安全门槛；SPEED 至少达到按目标、轮径和 CPR 计算理论计数的 20%。不足时 `ERR STALL`、锁存堵转 fault 并记录 CAPTURE。测试必须抬轮并保持电池开关随手可断。
- IWDG 只在核心采样、状态推进、安全提交和必要 Service 完成后喂狗；受控 App FAULT 循环继续喂狗，不自动重启，恢复运动必须由下一次完整短按触发。
- IWDG 初始化先写 `0xCCCC` 启动 LSI，再解锁并写 PR/RLR；更新位等待有固定上限，超时使正式与 Bench 启动进入紧急停机和 `Error_Handler`。
- 丢线、线路无效、反馈超过 30 ms、控制失败和主循环间隔超过 100 ms属于可恢复 FAULT；TB6612/HAL/方向持续错误属于 FATAL，必须断电重启。
- 找线重捕必须连续 3 个新鲜样本、中间不得出现白帧/重复 sequence/零时差/超过 30 ms 间隔；首次白帧锁存的 1000 ms 总期限不会重开。
- 单轮堵转使用 300 ms 固定窗口净进展，达到目标理论计数的 20% 才通过；单帧 `+1` 抖动不能重置窗口。
- 方向 FATAL 仅在目标与控制器命令同号且物理计数反向时累计。正目标下的负 PWM 控制/制动不单独构成方向故障。
- 可恢复故障只有在线路非白后才有健康时间；`LINE_LOST` 还要求重新看到中心 `0x06` 并完成稳定窗口。OLED 异步重试错误同样只置显示降级。

## 测试

主机测试项目位于 `tests/`，每个模块使用独立假 HAL 和 CTest 用例。

```powershell
cmake -S tests/sc_line -B build/host-sc-line -G Ninja `
  -DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe
cmake --build build/host-sc-line
ctest --test-dir build/host-sc-line --output-on-failure
```

其余回归目录包括 `tests/sc_uart`、`tests/sc_i2c2`、`tests/sc_motor`、`tests/sc_track`、`tests/sc_wheel_service`、`tests/sc_pid`、`tests/sc_speed_controller`、`tests/sc_app`、`tests/sc_telemetry` 和 `tests/sc_bt_bench`。缺陷对照脚本位于对应测试目录的 `run_mutant_tests.ps1`。

## 后续工作

1. 在抬轮受控台架中使用 `sc_hc05_tune.ps1` 完成 3 轮 Identify，确认编码器方向、1040 count/rev、48 mm 轮径和电机符号。
2. 使用 RAW/SPEED/PID/CAPTURE 记录可追溯开环与闭环响应，完成低速 PID 实验；通过前保持两个 VALID 常量为 0。
3. 在完整比赛赛道复测当前循迹参数，并固定 LF04 安装高度和阈值。
4. 完成整车供电、长时间运行、温升、IWDG 复位可见性和比赛赛道测试。
