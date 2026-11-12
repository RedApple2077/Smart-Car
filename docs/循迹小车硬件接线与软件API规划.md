# 循迹小车硬件接线与软件 API 规划

## 1. 设计结论、范围与当前状态

本项目采用“单块实时控制核心、单块双通道电机驱动、双驱动轮速度闭环、四路红外离散循迹、MPU6050 短时航向辅助”的比赛基线。主控制链为：

`STM32F103C8T6 -> TB6612FNG -> MG310P20 左右电机`

`LF04 四路红外 -> 线型语义 -> 循迹策略 -> 左右轮目标速度 -> 双轮速度控制 -> 应用层安全门 -> TB6612FNG`

第二块 STM32F103C8T6、第二块 TB6612FNG、第二块 MPU6050 和第二个 HC-05 均作为备件。HC-SR04、SG90 云台和 PS/2 摇杆不进入比赛运行功能。面包板只用于原型验证，不作为比赛行驶时的可靠连接结构。

本文定义硬件连接、硬件模块边界、可实现 C 接口和验收方法，不提供已验证 PID 增益。当前 `Smart-Car.ioc` 已写入 SWD、SysTick、8 MHz HSE PLL 至 72 MHz、TIM2/TIM3 编码器接口、TIM4 双路 PWM、I2C2、USART2 和相应 GPIO；设备驱动、非阻塞 Service、Wheel Service、双轮速度控制、循迹 `mm/s` 目标策略、正式 `sc_app` 安全状态机、IWDG、遥测/OLED 和独立 HC-05 Bench 已完成。三套固件均可构建，正式链与 Bench 保持隔离。

### 1.1 比赛边界

- 赛道为白底黑线。车辆沿黑线行进，进入黑线组成的十字路口时保持直行。
- 比赛可以从赛道两端中的任意一端开始，因此车载软件不把某一端固定视为起点。
- 完赛计时、人工收车、三次机会、最好成绩和未完赛距离均由场外记录，不进入车载 API。
- “四轮完全离开赛道”或“四轮均在黑线同侧”的裁判判定由场外人员完成。LF04 只能提供黑线检测与丢线语义，不声称能直接观察四个车轮的接触状态。
- 终点不设置车载自动识别逻辑。车辆在裁判确认完赛或人工收车时退出运行。
- 比赛态禁止通过蓝牙、串口、OLED 或其他外部界面改变电机输出。所有控制状态只由车载控制链和安全状态产生。

### 1.2 非目标

- 不使用双板实时分布式控制，不让第二块 STM32 参与比赛控制。
- 不使用 HC-SR04、SG90 或 PS/2 摇杆完成比赛功能。
- 不将 MPU6050 当作绝对航向源，不使用长期陀螺仪积分替代 LF04。
- 不把 LF04 的离散死区输入描述为连续位置传感器。
- 不在本文填写 PID 增益、丢线找回时长、比赛速度或停车延时。
- 不修改现有 CubeMX、HAL、CMake、源码或硬件原件。

### 1.3 当前工程事实

以下事实核对于 2026-11-12。只记录当前配置、源码和已执行验证中可以确认的状态，不把驱动存在等同于设备在线、控制闭环或整车身验证已经完成。

- 工程路径：`C:\Users\RedApple\Documents\Smart-Car`。
- 工程格式：STM32CubeMX 生成的 HAL 工程，CMake 构建，C11 标准。`CMakePresets.json` 提供 `Debug`、`Release` 和独立 `Bench` 预设，使用 Ninja 与 `cmake/gcc-arm-none-eabi.cmake` 工具链。
- 当前 `.ioc` 器件：`STM32F103C8Tx`、LQFP48、STM32Cube FW_F1 V1.8.7。
- 当前时钟：8 MHz HSE 经 PLL ×9 得到 72 MHz SYSCLK/HCLK；APB1 为 36 MHz，APB2 为 72 MHz，APB1/APB2 定时器时钟均为 72 MHz。`SystemClock_Config()` 使用 `FLASH_LATENCY_2`。
- SWD 与系统节拍：`PA13` SWDIO、`PA14` SWCLK、SysTick。
- I2C2：`PB10/PB11` 为 SCL/SDA，标准模式 100 kHz，7 位地址。`sc_i2c2` 负责单一在途事务和回调分发；MPU6050 驱动完成 WHO_AM_I、量程配置、异步采样和自动校零；SSD1306 驱动完成 1024 B 显存、分页分块异步刷新。
- TIM2：`PA0/PA1` 对应 `TIM2_CH1/CH2`，编码器模式 `TIM_ENCODERMODE_TI12`，输入滤波参数 10、预分频 0、周期 65535。`sc_encoder` 已启动计数并实现 16 位回绕、失同步冻结和计数重置。
- TIM3：`PA6/PA7` 对应 `TIM3_CH1/CH2`，配置与 TIM2 相同，并纳入同一个双路编码器快照。
- TIM4：`PB6/PB7` 对应 `TIM4_CH1/CH2`，PWM1 模式，预分频 0、周期 3599，定时器时钟 72 MHz 时 PWM 频率约 20 kHz。`sc_tb6612` 已启动双路 PWM，实现方向、STBY、零输出滑行和安全停机。
- USART2：`PA2/PA3` 为 TX/RX，9600 8N1，无硬件流控。`sc_uart_init(baud, rx_enable)` 只服务这一端口；正式固件禁用接收，Bench 固件启用单字节中断接收。发送使用 TX 环形缓冲和 `HAL_UART_Transmit_IT()`，驱动提供溢出及线路错误统计。
- LF04：`PB12..PB15` 对应 `O1..O4` 上拉输入。`sc_lf04` 校验 GPIOB 配置并从单次 `IDR` 快照采样；`sc_line_service` 输出 `raw_levels`、`black_mask` 和完整语义枚举，正式主循环持续更新缓存但不执行电机控制。
- 电机与状态输出：`PA4/AIN1`、`PA5/AIN2`、`PB0/BIN1`、`PB1/BIN2`、`PB8/TB6612_STBY`、`PB9/STATUS_LED` 均为推挽输出。正式固件初始化 TB6612 后保持 STBY 低。
- 正式主循环：每 10 ms 推进 LF04/线路语义；每轮服务编码器，仅在新 sequence 时进入 Wheel Service；仅在新的有效双轮反馈上推进速度控制；`sc_app` 应用启动门、状态机、故障检测和唯一执行器提交。遥测 Service 在核心控制之后推进，IWDG 在全部必要 Service 完成后最后喂狗。
- Bench 主循环：保留原有 LF04、编码器、蓝牙命令、心跳、MPU、OLED、UART 和限时单轮控制；同样只使用 `sc_safety_iwdg_init/feed` 接入看门狗。
- 正式/Bench 隔离：正式 Debug/Release 不链接 `sc_bt_bench`，不包含蓝牙运动命令；Bench 产物为 `build/Bench/Smart-Car-HC05Bench.elf`，提供 `PING/STATUS/LINE/LF/ARM/MOTOR/STOP/RESET/HELP`。
- 当前测试状态：UART 13/13 且缺陷变体 3/3；按键 3/3 且缺陷变体 1/1；Safety 3/3 且缺陷变体 1/1；Wheel Service 8/8 且缺陷变体 6/6；PID 6/6 且缺陷变体 4/4；双轮速度控制 9/9 且缺陷变体 6/6；循迹目标策略 13/13 且缺陷变体 7/7；I2C/MPU/OLED 15/15 且缺陷变体 13/13；TB6612/编码器 24/24 且缺陷变体 18/18；LF04/线路语义 13/13 且缺陷变体 11/11；App 24/24 且缺陷变体 10/10；Telemetry 8/8 且缺陷变体 5/5；Bench 31/31 且缺陷变体 13/13。全量 13 个主机套件共 170/170 通过，App/Track/Telemetry/Safety 通过 GCC `-fanalyzer`，Debug/Release/Bench 均通过交叉编译。
- 当前硬件验证：TB6612 单轮短脉冲、左右通道隔离、自动停机、编码器计数和右电机方向纠正已完成台架验证；HC-05 SPP 已完成命令交互。MPU6050/OLED 的最终整车隔离、LF04 实物识别、双轮速度闭环、长稳、供电余量和赛道表现仍未验证。
- 当前源码边界：正式闭环、应用安全状态机、故障恢复、IWDG、HC-05 5 Hz 遥测、2 Hz OLED、状态 LED、自动校零、独立 Bench 已实现。持久化配置 Service 和 Bench 闭环调参命令仍未实现；当前只有编译期默认参数与两个显式 VALID 标志。`SC_MOTOR_CALIBRATION_VALID` 和 `SC_MOTOR_TUNING_VALID` 必须保持 0，直到实物实验产生可追溯证据。

## 2. 44 项物料状态表

数量与原清单一致。状态使用本文统一的六类标记：比赛主链路、辅助/调试、台架工具、备件、维护件、不用。

| 序号 | 物料与数量 | 状态 | 规划用途 |
|---:|---|---|---|
| 1 | ST-Link 调试器（SWD）×1 | 台架工具 | 只用于离线烧录；烧录时电池、Type-C 和外设负载断开，以 ST-Link 3.3V 作为唯一目标电源。比赛和在线运行不接；在线状态只通过 OLED 或 HC-05 查看。 |
| 2 | STM32F103C8T6 开发板（Type-C 接口）×2 | 比赛主链路 | 1 块作为实时控制核心；第 2 块作为备件。 |
| 3 | 830 孔面包板 ×1 | 台架工具 | 仅做原型与电气检查，不用于比赛车的最终固定连接。 |
| 4 | LED 红发光二极管 ×3 | 辅助/调试 | 1 只经 510Ω 接 `PB9` 作状态灯；其余 2 只备用。 |
| 5 | 510Ω 直插电阻 ×3 | 辅助/调试 | 1 只用于 `PB9` LED 限流；其余 2 只备用。 |
| 6 | 6×6×5 轻触开关 ×20只 | 比赛主链路 | 1 只接 `PB5` 作待命/发车输入；其余作为备件和台架按钮。 |
| 7 | 杜邦线 10cm 公对公 ×20条 | 台架工具 | 面包板和短距测试；不作为振动环境下的最终可靠线束。 |
| 8 | 杜邦线 10cm 母对母 ×20条 | 台架工具 | 台架转接与模块测试；不替代锁扣线束或焊接。 |
| 9 | 杜邦线 10cm 公对母 ×20条 | 台架工具 | 台架转接与探头测试。 |
| 10 | 杜邦线 20cm 公对公 ×20条 | 台架工具 | 较长距离原型连接，最终车体使用固定线束。 |
| 11 | 杜邦线 20cm 公对母 ×20条 | 台架工具 | 较长距离原型连接，最终车体使用固定线束。 |
| 12 | USB-A 转 Micro-USB 数据线 ×1条 | 台架工具 | 通用 Micro-USB 数据线；本车不保留额外有线串口调试链路。 |
| 13 | PH2.0 6P 转 XH2.54 6P 双头线 20cm ×2条 | 比赛主链路 | 两台 MG310 预装线束的转接基础，每个电机 1 条；最终需固定并做应力释放。 |
| 14 | PH2.0 6P 单头线 20cm ×2条 | 备件 | 线束返修或台架测试备用。 |
| 15 | XH2.54 6P 弯插座 ×2个 | 备件 | 电机/编码器线束返修或替换用，不进入基线。 |
| 16 | XH2.54 6P 直插座 ×2个 | 备件 | 与弯插座互为安装方向备件，不重复占用。 |
| 17 | TB6612FNG 电机驱动模块（焊接）×2个 | 比赛主链路 | 1 块用 A/B 两路驱动左右电机；第 2 块作备件和单路热裕量替代件，不并联输出增流。 |
| 18 | MG310 直流减速电机（含霍尔编码器）×2台 | 比赛主链路 | 左右驱动轮，含 1:20 减速、7.4V 电机、13ppr 霍尔编码器。 |
| 19 | MG310 电机 U 型支架 ×1个 | 比赛主链路 | 作为左右电机的一组安装支撑结构，按底板长孔和轴距固定；不假设每个电机各有一个独立支架。 |
| 20 | 48mm 橡胶轮胎 ×2个 | 比赛主链路 | 左右驱动轮轮胎，名义滚动直径 48 mm。 |
| 21 | 1 英寸万向轮 ×2个 | 比赛主链路 | 作为前端两个被动支撑轮，与后端左右驱动轮组成四轮两驱布局。 |
| 22 | R1 Mini 四轮两驱 3mm 磨砂亚克力底板 ×1块 | 比赛主链路 | 主结构底板；后端左右驱动轮，前端两个万向轮，LF04 固定在前缘中线下方。 |
| 23 | R1 系列三轮/四轮两驱小车底板（适配 MG310）×1块 | 备件 | 第 22 项孔位或结构不适配时的替代底板；不预设两层固定叠装方式。 |
| 24 | M3×4+6 单通铜柱 ×8个 | 比赛主链路 | 传感器、控制板或局部屏蔽的固定与支撑；按实际孔位分配。 |
| 25 | M3×4 银色十字圆头螺丝 ×8个 | 比赛主链路 | 配合铜柱和底板固定。 |
| 26 | M3×8 黑色十字圆头螺丝 ×4个 | 比赛主链路 | 适合较厚叠层的固定点；不得穿透轮胎或干涉电机。 |
| 27 | M3 黑色六角螺母 ×12个 | 比赛主链路 | 铜柱、支架和底板的紧固配套。 |
| 28 | AMS1117-5.0V 降压稳压模块（插针）×2个 | 比赛主链路 | 1 块把 2S 电池降到 5V；第 2 块作备件，不并联输出。 |
| 29 | 德力普 18650 锂电池（14400mWh，尖头）×2节 | 比赛主链路 | 两节串联组成 2S 动力电池，标称 7.2V，满电 8.4V。 |
| 30 | 18650 两节串联带盖带开关电池盒 ×3个 | 比赛主链路 | 1 个作为比赛电池仓和主电源开关；其余 2 个作备件。 |
| 31 | 2 节 18650 电池充电器 ×1 | 台架工具 | 独立充电，充电时不得连接小车任何电路。 |
| 32 | 船形开关（三档黑色，不带线）×3个 | 备件 | 基线由电池盒开关做总断电，不把三档开关作为比赛必需件。 |
| 33 | 船形开关（两档黑色，不带线）×3个 | 备件 | 可作为维护开关备件；基线不串接第二个总电源开关。 |
| 34 | 船形开关（两档红色，不带线）×3个 | 备件 | 可作为维护开关备件；不得在带电时改线。 |
| 35 | HC-05 蓝牙模块（带底板、焊排针）×2个 | 辅助/调试 | 1 个可选用于 9600 波特率只读遥测；第 2 个作备件。比赛态不接受无线电机控制。 |
| 36 | USB 转 TTL 串口模组 ×1 | 不用 | 基线只保留 HC-05 的 USART2；该模组不接线、不供电，不进入固件、测试或整车。 |
| 37 | SG90 9G 舵机（90°–180°）×1个 | 不用 | 比赛规则无转向舵机需求，且会增加 5V 电流和稳压发热。 |
| 38 | HC-SR04 超声波模块 ×1个 | 不用 | 比赛规则无避障需求，Echo 为 5V 信号，不进入 3.3V 主控链路。 |
| 39 | SG90/HC-SR04 云台支架组件 ×1套 | 不用 | 与不采用的舵机和超声波配套，不进入比赛机械结构。 |
| 40 | MPU6050 沉金版六轴姿态模块 ×2个 | 辅助/调试 | 1 个可选接 I2C2，只提供短时 gyro-Z 航向辅助；第 2 个作备件。 |
| 41 | 0.96 英寸白色 OLED 显示模块（128×64，I2C，4P，SSD1306）×1块 | 辅助/调试 | 接 I2C2 显示状态、原始线型、速度和故障，刷新不得阻塞控制。 |
| 42 | PS2 双轴模拟摇杆 ×1 | 不用 | 不提供比赛人工转向；仅可在独立台架中另议，不进入本车接口。 |
| 43 | LF04 四路红外巡线模块 ×1个 | 比赛主链路 | 主循迹传感器，安装高度约 3 cm，VCC 使用 3.3V。 |
| 44 | 德力西黑色 PVC 电工胶布（10yd）×2卷 | 维护件 | 线束标记、临时固定和应力释放辅助；不覆盖散热孔，不替代保险丝。 |

## 3. 系统拓扑

### 3.1 电源树

```text
2S 18650 电池组（标称 7.2 V，满电 8.4 V）
└─ 电池盒串联输出与盒盖开关
   ├─ TB6612FNG VM
   └─ AMS1117-5.0 模块 VIN+
      └─ 5.0 V
         ├─ STM32F103C8T6 核心板 5V 引脚
         └─ HC-05 底板 VCC（3.6-6 V 输入范围，板上再降压）

STM32F103C8T6 3.3V 引脚
├─ TB6612FNG VCC
├─ LF04 VCC
├─ 左 MG310 编码器 VCC
├─ 右 MG310 编码器 VCC
├─ MPU6050 VCC
├─ SSD1306 OLED VCC
└─ LED 高侧供电由 PB9 提供，LED 阴极经 510Ω 到 GND

所有电源负极、TB6612 PGND/GND、核心板 GND 与各模块 GND 共地。
```

2S 电池只给电机驱动 VM 和 AMS1117-5 输入供电。AMS1117-5 的 5V 输出只承载核心板与 HC-05，不给电机或舵机供电。核心板 3.3V 只承载逻辑外设。两块 AMS1117 不得并联输出。

### 3.2 运行数据流

```text
LF04──────────┐
编码器 L/R────┼─> BSP/HAL 采样 ─> Driver 数据 ─> Service 快照
MPU6050───────┤                                    │
按键──────────┘                                    v
                                            Control 目标与运动请求
                                                   │
                                                   v
                                     Application 安全门与运行快照
                                                   │
                                                   v
                                      TB6612 Driver 唯一执行器出口
                                                   │
                                                   v
                                               左右电机

OLED / HC-05 <── 只读遥测快照
```

MPU6050、OLED 和 HC-05 都是旁路设备。它们失效时不能阻断 LF04、左右编码器、TB6612 和启动按键组成的基本比赛链。

### 3.3 软件层次依赖

```text
BSP/HAL
   ^
   │ 仅向上提供硬件原语
Driver
   ^
   │ 设备状态与采样
Service
   ^
   │ 统一单位、校准、控制和遥测服务
Control
   ^
   │ 循迹与运动请求
Application
   │ 启动、停止、唯一执行器提交
   v
TB6612 Driver
```

箭头表示允许的依赖方向。Driver 不调用 Service 或 Control，Service 不修改硬件寄存器，Control 不直接访问 GPIO/I2C/UART，Application 不绕过 TB6612 Driver 写电机引脚。

### 3.4 STM32F103C8T6 资源分配

| 功能 | MCU 引脚/外设 | 模式 | 说明 |
|---|---|---|---|
| 左编码器 A | `PA0 / TIM2_CH1` | 编码器接口 | 输入上拉，边沿由定时器计数。 |
| 左编码器 B | `PA1 / TIM2_CH2` | 编码器接口 | 与 A 组成四倍频正交计数。 |
| 右编码器 A | `PA6 / TIM3_CH1` | 编码器接口 | 输入上拉，边沿由定时器计数。 |
| 右编码器 B | `PA7 / TIM3_CH2` | 编码器接口 | 与 A 组成四倍频正交计数。 |
| 左电机 PWM | `PB6 / TIM4_CH1` | 复用推挽 PWM | 连接 TB6612 PWMA。 |
| 右电机 PWM | `PB7 / TIM4_CH2` | 复用推挽 PWM | 连接 TB6612 PWMB。 |
| 左电机方向 1 | `PA4` | GPIO 输出 | 连接 TB6612 AIN1，初始化先置低。 |
| 左电机方向 2 | `PA5` | GPIO 输出 | 连接 TB6612 AIN2，初始化先置低。 |
| 右电机方向 1 | `PB0` | GPIO 输出 | 连接 TB6612 BIN1，初始化先置低。 |
| 右电机方向 2 | `PB1` | GPIO 输出 | 连接 TB6612 BIN2，初始化先置低。 |
| 驱动使能 | `PB8` | GPIO 输出 | 连接 TB6612 STBY，初始化先置低。 |
| LF04 O1 | `PB12` | GPIO 输入 | 按车体左到右第 1 路。 |
| LF04 O2 | `PB13` | GPIO 输入 | 按车体左到右第 2 路。 |
| LF04 O3 | `PB14` | GPIO 输入 | 按车体左到右第 3 路。 |
| LF04 O4 | `PB15` | GPIO 输入 | 按车体左到右第 4 路。 |
| I2C2 SCL | `PB10` | 复用开漏 | MPU6050 与 OLED 共用，外部上拉只回 3.3V。 |
| I2C2 SDA | `PB11` | 复用开漏 | MPU6050 与 OLED 共用，外部上拉只回 3.3V。 |
| HC-05 RXD | `PA2 / USART2_TX` | 复用推挽 | MCU 发送到蓝牙 RXD。 |
| HC-05 TXD | `PA3 / USART2_RX` | 复用输入 | 蓝牙 TXD 发送到 MCU。 |
| 发车按键 | `PB5` | GPIO 输入上拉 | 按键另一端接 GND，按下为低。 |
| 状态 LED | `PB9` | GPIO 输出 | 高电平点亮，串联 510Ω 到 GND。 |
| SWDIO | `PA13` | SWD | 仅离线烧录。 |
| SWCLK | `PA14` | SWD | 仅离线烧录。 |

资源检查结果：上述 MCU 功能引脚互不重复；TIM2/TIM3 只作编码器模式，TIM4 只作双路 PWM；I2C2 与 USART2 同电机控制引脚不共用。`PA9/PA10`、`PA11/PA12`、`PC13`、TIM1、USART3、I2C1 保留，不进入基线。

## 4. 接线表

### 4.1 接线总则

- 所有模块的逻辑输入电压不得超过 3.3V。HC-05 底板 VCC 可接系统 5V，但其 RXD/TXD 是 3.3V TTL。
- LF04 VCC 使用 3.3V；整套手册同时描述 TTL 和可能的宽压模块，本车固定按 3.3V 逻辑供电。
- 两个 MG310 编码器 VCC 只接 3.3V。编码器输出幅值跟随其供电，接 5V 会把 AB 相抬高到 5V。
- I2C2 的 SCL/SDA 上拉必须回到 3.3V。不得让 OLED、MPU6050 或底板上的 5V 上拉进入 MCU。
- UART 必须交叉：模块 TXD 接 MCU RX，模块 RXD 接 MCU TX。
- TB6612 的 VM 是电机电源，VCC 是逻辑电源；两者不可互换。
- 目标板的 5V/3.3V 电源轨任一时刻只允许一个供电源；电池、Type-C 和 ST-Link 3.3V 不得并联。额外 USB 转 TTL 模组不进入本车串口或供电链路。
- 每个模块的 GND 都必须回到同一系统 GND。接插前排空电源，首次上电采用限流或快速断电检查。

### 4.2 电池、稳压与核心板

| 模块 | 模块引脚 | STM32/目标 | 方向 | 电平 | 备注 |
|---|---|---|---|---|---|
| 2S 电池盒 | 串联正极输出 | TB6612 `VM` | 电源输出 | 7.2V 标称，8.4V 满电 | 经过电池盒开关；先分配动力支路。 |
| 2S 电池盒 | 串联正极输出 | AMS1117-5 `VIN+` | 电源输出 | 7.2V 标称，8.4V 满电 | 与 VM 支路并联，但稳压模块不得输出反灌电池。 |
| 2S 电池盒 | 负极 | 系统 `GND` | 电源回流 | 0V | TB6612、稳压器、核心板和全部模块共地。 |
| AMS1117-5 | `VOUT+ / 5V` | 核心板 `5V` | 电源输出 | 5.0V | 核心板由板载 LDO 产生 3.3V。 |
| AMS1117-5 | `VOUT+ / 5V` | HC-05 底板 `VCC` | 电源输出 | 5.0V | HC-05 底板标称 3.6-6V；裸模块不能按此接法供电。 |
| AMS1117-5 | `GND` | 系统 `GND` | 回流 | 0V | 不得悬空。 |
| 核心板 | `3.3V` | TB6612 `VCC` | 电源输出 | 3.3V | 逻辑电源。 |
| 核心板 | `3.3V` | LF04 `VCC` | 电源输出 | 3.3V | 不接 5V。 |
| 核心板 | `3.3V` | 左编码器 `VCC` | 电源输出 | 3.3V | AB 输出随之为 3.3V。 |
| 核心板 | `3.3V` | 右编码器 `VCC` | 电源输出 | 3.3V | 与左编码器同一逻辑电源。 |
| 核心板 | `3.3V` | MPU6050 `VCC` | 电源输出 | 3.3V | 只使用 3.3V。 |
| 核心板 | `3.3V` | OLED `VCC` | 电源输出 | 3.3V | 4P I2C 模块。 |
| 核心板 | 任一 `GND` | 系统 `GND` | 回流 | 0V | 所有 GND 形成星形或低阻抗公共回流。 |

电池盒开关是唯一总断电点。运行和比赛期间不连接 Type-C，也不连接 ST-Link；台架烧录时电池、Type-C 和全部外设负载断开，ST-Link 的 3.3V 作为唯一目标电源。

### 4.3 TB6612 与电机

| 模块 | 模块引脚 | STM32/目标 | 方向 | 电平 | 备注 |
|---|---|---|---|---|---|
| TB6612 | `PWMA` | `PB6 / TIM4_CH1` | MCU 输出 | 3.3V PWM | 左电机 A 通道 PWM。 |
| TB6612 | `AIN1` | `PA4` | MCU 输出 | 3.3V | 左通道方向 1，初始化低。 |
| TB6612 | `AIN2` | `PA5` | MCU 输出 | 3.3V | 左通道方向 2，初始化低。 |
| TB6612 | `PWMB` | `PB7 / TIM4_CH2` | MCU 输出 | 3.3V PWM | 右电机 B 通道 PWM。 |
| TB6612 | `BIN1` | `PB0` | MCU 输出 | 3.3V | 右通道方向 1，初始化低。 |
| TB6612 | `BIN2` | `PB1` | MCU 输出 | 3.3V | 右通道方向 2，初始化低。 |
| TB6612 | `STBY` | `PB8` | MCU 输出 | 3.3V | 初始化先低；只有应用层允许时才可置高。 |
| TB6612 | `VM` | 2S 电池经开关正极 | 电源输入 | 7.2-8.4V | 不得接 5V 逻辑电源。 |
| TB6612 | `VCC` | 核心板 `3.3V` | 电源输入 | 3.3V | 逻辑电源。 |
| TB6612 | `GND` 逻辑侧 | 系统 `GND` | 回流 | 0V | 与 PGND 共用系统地。 |
| TB6612 | `GND` 功率侧 | 系统 `GND` | 回流 | 0V | 大电流回流线直接回电池负极。 |
| TB6612 | `AO1` | 左电机 `M+` | 电机输出 | PWM 斩波 | 左轮动力线。 |
| TB6612 | `AO2` | 左电机 `M-` | 电机输出 | PWM 斩波 | 左轮动力线。 |
| TB6612 | `BO1` | 右电机 `M+` | 电机输出 | PWM 斩波 | 右轮动力线。 |
| TB6612 | `BO2` | 右电机 `M-` | 电机输出 | PWM 斩波 | 右轮动力线。 |

单块 TB6612 的 A/B 两路分别驱动左右电机。第二块 TB6612 不并联输出，不尝试通过双 H 桥并联增加电流。若单块模块实测温升过高，应改成一块模块分别驱动一个电机，并重新分配 `STBY` 和方向控制；该替代属于硬件维护方案，不进入当前接线基线。

### 4.4 MG310P20 与霍尔编码器

每个电机端子板按丝印连接，不按线色猜测功能。

| 模块 | 模块引脚 | STM32/目标 | 方向 | 电平 | 备注 |
|---|---|---|---|---|---|
| 左 MG310 | `M+` | TB6612 `AO1` | 电机动力 | 7.2-8.4V PWM | 电机端子。 |
| 左 MG310 | `M-` | TB6612 `AO2` | 电机动力 | 0V/PWM | 电机端子。 |
| 左 MG310 | `VCC` | 核心板 `3.3V` | 电源输入 | 3.3V | 编码器供电，绝缘与线束检查后上电。 |
| 左 MG310 | `GND` | 系统 `GND` | 回流 | 0V | 与电机功率地共地。 |
| 左 MG310 | `A` | `PA0 / TIM2_CH1` | 编码器输出 | 3.3V 方波 | 相序以正向前进时计数递增为标定目标。 |
| 左 MG310 | `B` | `PA1 / TIM2_CH2` | 编码器输出 | 3.3V 方波 | 与 A 相差约 90°。 |
| 右 MG310 | `M+` | TB6612 `BO1` | 电机动力 | 7.2-8.4V PWM | 电机端子。 |
| 右 MG310 | `M-` | TB6612 `BO2` | 电机动力 | 0V/PWM | 电机端子。 |
| 右 MG310 | `VCC` | 核心板 `3.3V` | 电源输入 | 3.3V | 编码器供电。 |
| 右 MG310 | `GND` | 系统 `GND` | 回流 | 0V | 与电机功率地共地。 |
| 右 MG310 | `A` | `PA6 / TIM3_CH1` | 编码器输出 | 3.3V 方波 | 相序以正向前进时计数递增为标定目标。 |
| 右 MG310 | `B` | `PA7 / TIM3_CH2` | 编码器输出 | 3.3V 方波 | 与 A 相差约 90°。 |

MG310P20_7.4V 名义参数为：减速比 1:20、额定电压 7.4V、额定电流不大于 0.5A、堵转电流不大于 2.0A、霍尔编码器 13ppr、编码器供电 3.3-5V。本车固定使用 3.3V。48 mm 轮径的名义计算关系为：

- 减速后每转脉冲数：`13 × 20 = 260 pulse/rev`。
- 四倍频计数：`260 × 4 = 1040 count/rev`。
- 名义轮周长：`π × 48 = 150.8 mm/rev`。
- 名义距离分辨率：`150.8 ÷ 1040 = 0.145 mm/count`。

上述数值是名义计算值，最终以轮径标定结果和汇编配置为准。TB6612 单通道连续平均电流为 1.2A，峰值 3.2A；两电机同时堵转时总需求可能超过单块驱动能力，所以禁止长时间堵转，并必须实测模块温升。

### 4.5 LF04 四路红外

| 模块 | 模块引脚 | STM32/目标 | 方向 | 电平 | 备注 |
|---|---|---|---|---|---|
| LF04 | `VCC` | 核心板 `3.3V` | 电源输入 | 3.3V | 固定 3.3V，不接 5V。 |
| LF04 | `GND` | 系统 `GND` | 回流 | 0V | 与核心板共地。 |
| LF04 | `O1` | `PB12` | 传感器输出 | 3.3V TTL | 车体最左侧。 |
| LF04 | `O2` | `PB13` | 传感器输出 | 3.3V TTL | 从左第 2 路。 |
| LF04 | `O3` | `PB14` | 传感器输出 | 3.3V TTL | 从左第 3 路。 |
| LF04 | `O4` | `PB15` | 传感器输出 | 3.3V TTL | 车体最右侧。 |

LF04 安装在车体前部，探头对地约 3 cm。先把模块放在白底上，调节各通道电位器直到输出为高；再逐路移到黑线上，确认该路变低。四路传感器在车体上从左到右必须与 `O1-O4` 同序。

### 4.6 I2C2：MPU6050 与 OLED

| 模块 | 模块引脚 | STM32/目标 | 方向 | 电平 | 备注 |
|---|---|---|---|---|---|
| MPU6050 | `VCC` | 核心板 `3.3V` | 电源输入 | 3.3V | 不接 5V。 |
| MPU6050 | `GND` | 系统 `GND` | 回流 | 0V | 共地。 |
| MPU6050 | `SCL` | `PB10 / I2C2_SCL` | I2C 总线 | 3.3V 开漏 | 与 OLED 共享。 |
| MPU6050 | `SDA` | `PB11 / I2C2_SDA` | I2C 总线 | 3.3V 开漏 | 与 OLED 共享。 |
| MPU6050 | `AD0` | `GND` | 地址选择 | 0V | 7 位地址 `0x68`。 |
| MPU6050 | `XDA/XCL/INT` | 不连接 | 保留 | - | 不进入基线；`INT` 不用于控制周期。 |
| OLED | `GND` | 系统 `GND` | 回流 | 0V | 4P 模块。 |
| OLED | `VCC` | 核心板 `3.3V` | 电源输入 | 3.3V | 不接 5V。 |
| OLED | `SCL` | `PB10 / I2C2_SCL` | I2C 总线 | 3.3V 开漏 | 与 MPU6050 共享。 |
| OLED | `SDA` | `PB11 / I2C2_SDA` | I2C 总线 | 3.3V 开漏 | 与 MPU6050 共享。 |

当前 OLED 模块背面标有 `0x78` 和 `0x7A`，分别对应 7 位地址 `0x3C` 和 `0x3D`；基线使用 `0x3C`。MPU6050 的 `AD0` 接地，7 位地址为 `0x68`。初始化必须扫描 I2C2 并核对这两个地址。I2C 上拉只回 3.3V，模块自带上拉时不得再外接 5V 上拉。

I2C2 基线速率先采用 100 kHz 标准模式。MPU6050 和 OLED 都必须分别设置访问超时；OLED 刷新不能占用整个控制周期。

### 4.7 按键、LED 与 SWD

| 模块 | 模块引脚 | STM32/目标 | 方向 | 电平 | 备注 |
|---|---|---|---|---|---|
| 轻触开关 | 一端 | `PB5` | MCU 输入 | 上拉为高 | 内部上拉。 |
| 轻触开关 | 另一端 | 系统 `GND` | 回流 | 0V | 按下为低，软件消抖。 |
| 红色 LED | 阳极 | `PB9` | MCU 输出 | 高电平点亮 | 输出电流受 510Ω 限制。 |
| 红色 LED | 阴极 | 510Ω 一端 | 串联 | - | 电阻另一端接 GND。 |
| 510Ω 电阻 | 另一端 | 系统 `GND` | 回流 | 0V | 不可直接短接 LED 到 GND。 |
| ST-Link | `GND` | 核心板 `GND` | 参考地 | 0V | 仅离线烧录连接。 |
| ST-Link | `3.3V` | 核心板 `3.3V` | 唯一电源输出 | 3.3V | 用户已实测可完成烧录；电池、Type-C、外设负载断开。 |
| ST-Link | `SWDIO` | `PA13` | 双向数据 | 3.3V | 仅离线烧录使用。 |
| ST-Link | `SWCLK` | `PA14` | 时钟 | 3.3V | 仅离线烧录使用。 |

### 4.8 串口与台架

| 模块 | 模块引脚 | STM32/目标 | 方向 | 电平 | 备注 |
|---|---|---|---|---|---|
| HC-05 底板 | `VCC` | AMS1117-5 `5V` | 电源输入 | 5.0V | 底板标注输入范围 3.6-6V。 |
| HC-05 底板 | `GND` | 系统 `GND` | 回流 | 0V | 共地。 |
| HC-05 底板 | `RXD` | `PA2 / USART2_TX` | 蓝牙输入 | 3.3V TTL | MCU 发送到蓝牙。 |
| HC-05 底板 | `TXD` | `PA3 / USART2_RX` | 蓝牙输出 | 3.3V TTL | 蓝牙发送到 MCU。 |
| HC-05 底板 | `STATE` | 不连接 | 状态输出 | - | 仅板载指示灯使用，不参与控制。 |
| HC-05 底板 | `EN` | 不连接 | 保持默认 | - | 不在比赛态切换 AT 模式。 |

HC-05 串口固定为 `9600 8N1`。正式固件调用 `sc_uart_init(9600u, 0u)`，只保留发送能力；独立 Bench 固件调用 `sc_uart_init(9600u, 1u)`，仅用于受控台架命令。正式固件即使收到任何字符，也不得改变目标速度、方向或电机使能。

### 4.9 线束和机械连接

- 左右电机使用 6P 电机线束，先核对端子方向、锁扣和每根线的丝印功能，再插入转接件。
- 电机线、编码器线和电源线分开走线；编码器线远离电机 PWM 输出和大电流回流线。
- 车体上的控制板、LF04 和接线端必须有机械固定。杜邦线仅可用于台架，不直接承受行驶振动。
- LF04 的 3 cm 安装高度、左右顺序和前视位置属于装配几何，安装后必须做四路逐点黑白检查。
- 主底板与备选底板不预设叠装。若实际需要双层结构，先核对孔位、轮径、离地间隙和重心，再决定层间铜柱位置。

## 5. LF04 真值表示例与数据定义

### 5.1 三层表示

模块原始输出是四个彼此独立的 TTL 电平，不是连续距离，也不是可互换的普通位图。

1. `raw_levels`：按 `O1 O2 O3 O4` 排列的原电平字符串，`0=黑线`，`1=白底`。
2. `black_mask`：检测到黑线为 1，`bit0=O1`、`bit1=O2`、`bit2=O3`、`bit3=O4`。
3. `semantic_state`：把归一化结果解释为直行、左/右直角弯候选、左/右大弯候选、左/右微调候选、十字候选、丢线候选或异常。

转换关系为：

```text
black_mask = ((O1 == 0) << 0) |
             ((O2 == 0) << 1) |
             ((O3 == 0) << 2) |
             ((O4 == 0) << 3)
```

`raw_levels` 和 `black_mask` 在内存中都使用低四位，且都按 `bit0=O1`、`bit1=O2`、`bit2=O3`、`bit3=O4` 存放。下文表格按 `O1 O2 O3 O4` 从左到右显示，便于与车体方向核对；十六进制只用于数值传输。

表格同时保留 `raw_levels` 和 `black_mask` 的 `O1..O4` 顺序，避免把未取反的原电平直接当作内部黑线位图。

### 5.2 手册位型

| 原电平 `O1 O2 O3 O4` | 黑线掩码 `O1 O2 O3 O4` | 掩码十六进制 | 语义状态枚举 |
|---|---|---|---|
| `1001` | `0110` | `0x06` | `SC_LINE_STRAIGHT` |
| `0001` | `1110` | `0x07` | `SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE` |
| `1000` | `0111` | `0x0E` | `SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE` |
| `0111` | `1000` | `0x01` | `SC_LINE_LEFT_LARGE_BEND_CANDIDATE` |
| `1110` | `0001` | `0x08` | `SC_LINE_RIGHT_LARGE_BEND_CANDIDATE` |
| `1011` | `0100` | `0x02` | `SC_LINE_LEFT_FINE_ADJUST_CANDIDATE` |
| `1101` | `0010` | `0x04` | `SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE` |
| `0000` | `1111` | `0x0F` | `SC_LINE_JUNCTION_CANDIDATE` |
| `1111` | `0000` | `0x00` | `SC_LINE_LOST_CANDIDATE` |

注意：`0000` 与 `1111` 取反后分别为 `1111` 与 `0000`；`0001` 的黑线掩码是 `1110`，而 `0111` 的黑线掩码是 `1000`。因此分类时必须同时使用原电平顺序和上下文，不能只保存一个不带位序说明的十六进制数。内部 API 必须保存 `raw_levels`、`black_mask` 和 `semantic_state` 三个字段。

`0000` 与 `1111` 是两个不同状态：前者代表四路都检测到黑线，后者代表四路都未检测到黑线。软件不得把二者合并成同一个“全白/全黑”或“无有效模式”占位。

Control 只使用 Service 返回的 `semantic_state`；不得再读取 `raw_levels` 做第二次线型分类。

其余七种四位组合不赋予控制含义，统一归为 `SC_LINE_UNKNOWN_PATTERN`，保留原始值供调试：

| 原电平 `O1 O2 O3 O4` | 黑线掩码 `O1 O2 O3 O4` | 掩码十六进制 | 语义状态枚举 |
|---|---|---|---|
| `0010` | `1101` | `0x0B` | `SC_LINE_UNKNOWN_PATTERN` |
| `0011` | `1100` | `0x03` | `SC_LINE_UNKNOWN_PATTERN` |
| `0100` | `1011` | `0x0D` | `SC_LINE_UNKNOWN_PATTERN` |
| `0101` | `1010` | `0x05` | `SC_LINE_UNKNOWN_PATTERN` |
| `0110` | `1001` | `0x09` | `SC_LINE_UNKNOWN_PATTERN` |
| `1010` | `0101` | `0x0A` | `SC_LINE_UNKNOWN_PATTERN` |
| `1100` | `0011` | `0x0C` | `SC_LINE_UNKNOWN_PATTERN` |

`SC_LINE_INVALID` 只表示采样失败或 `valid=0`；`SC_LINE_UNKNOWN_PATTERN` 表示四个采样位有效，但组合属于上表的保留集合。两者不得互换。

## 6. 逐硬件功能模块

| 模块 | 职责 | 主要输入 | 主要输出 | 依赖 | 失败或缺省处理 |
|---|---|---|---|---|---|
| STM32F103C8T6 核心板 | 采样、控制、通信、状态管理和唯一执行器提交 | 电池、LF04、编码器、MPU、按键 | PWM、方向、STBY、UART、I2C、LED | 3.3V 逻辑电源和 SWD | 初始化失败即不进入发车；时钟或复位异常进入硬件故障态。 |
| TB6612FNG | 把归一化 PWM 与方向转换为左右电机驱动 | PWM、方向、STBY、VM | AO1/AO2、BO1/BO2 | 3.3V VCC、2S VM、共地 | 未初始化、请求越界或驱动状态未就绪时保持 STBY 低；器件没有可读温度或故障输出，热风险由堵转测试和断电检查处理，不允许其他模块直接写其引脚。 |
| MG310P20 左电机 | 左驱动轮机械动力 | TB6612 A 通道 | 左轮转动 | 正确机械固定和轮径 | 方向配置错误、编码器可检测的堵转或闭环失效时记录硬件故障并停止发车；电流上限和温升仅作为台架测量项，不作车载在线承诺。 |
| MG310P20 右电机 | 右驱动轮机械动力 | TB6612 B 通道 | 右轮转动 | 正确机械固定和轮径 | 方向配置错误、编码器可检测的堵转或闭环失效时记录硬件故障并停止发车；电流上限和温升仅作为台架测量项，不作车载在线承诺。 |
| 左霍尔编码器 | 提供左轮速度与距离反馈 | 3.3V、A/B、轮轴转动 | TIM2 计数、方向、速度 | 编码器线束和方向标定 | 丢脉冲、零速卡死或计数停滞时使轮速闭环失效，应用层进入停止路径。 |
| 右霍尔编码器 | 提供右轮速度与距离反馈 | 3.3V、A/B、轮轴转动 | TIM3 计数、方向、速度 | 编码器线束和方向标定 | 同上。 |
| LF04 | 检测白底黑线并给出离散线型 | 3.3V、四路反射状态 | `raw_levels`、`black_mask`、语义状态 | 3 cm 安装高度、光照和电位器标定 | 读取无效或异常持续时判定核心传感故障，禁止发车；偶发全白保留为丢线候选。 |
| MPU6050 | 提供短时 gyro-Z 角速度测量 | 3.3V、I2C2、静止零偏 | 原始角速度、校准与有效性 | I2C2 地址 `0x68`；Service 负责生成短期航向增量 | 初始化、NACK 或数据无效时标记 `degraded`，循迹主链继续运行但不做航向辅助。 |
| SSD1306 OLED | 显示运行快照和故障原因 | I2C2 地址 `0x3C`、状态快照 | 文本/图形显示 | I2C2 总线和显示缓冲 | NACK 或刷新超时只关闭显示，不改变控制状态。 |
| HC-05 底板 | 发送调试遥测 | USART2、5V VCC、3.3V TTL | 只读遥测文本 | 5V 电源、UART 交叉和配对 | 未连接、断开或丢包时比赛控制仍可运行；比赛态忽略所有入站运动命令。 |
| USB 转 TTL 串口模组 | 不进入当前硬件基线 | 不接线 | 无 | 无 | 项目只保留 HC-05 的 USART2；不接入供电、信号或固件。 |
| ST-Link V2 | 离线烧录 | USB、SWD、3.3V | 离线烧录接口 | 唯一电源模式 | 比赛和正常电池运行均不连接；接口异常不影响已完成烧录的逻辑。 |
| AMS1117-5.0 | 将 2S 输入降至 5V | 7.2-8.4V 输入、GND | 5V 逻辑电源 | 电池开关和散热 | 输出异常会使核心板复位或掉电；重启应用保持待命，不能由第二块模块并联补流。 |
| 2S 电池组与电池盒 | 车辆动力和逻辑能量来源 | 两节 18650、电池盒开关 | 7.2-8.4V、可断电 | 极性、熔断/限流建议和机械固定 | 装配检查排除极性错误和电池仓松动；Type-C 与电池不得同时供电。 |
| 按键与 LED | 人工待命、发生请求和状态提示 | PB5 低电平、PB9 输出 | `arm/start` 请求、状态颜色 | 上拉、消抖、510Ω | 按键线路异常时应用保持待命；LED 失效只影响提示，不影响控制。 |
| 底盘、轮胎、万向轮、支架 | 承载传感器、电子件和驱动轮，维持接触姿态 | 底板、电机架、轮组、紧固件 | 机械运动基础 | 孔位匹配、重心和线束余量 | 机械松动、轮系干涉或传感器高度变化时停止运行；不把两块底板默认叠装。 |

第二块 STM32、第二块 TB6612、第二块 MPU6050 和第二个 HC-05 只在故障替换或台架并行验证时使用。HC-SR04、SG90、PS/2 摇杆和云台支架不接入本车。未来若加入台架摇杆，只能通过单独台架接口产生测试输入，比赛固件不能编译其控制路径。

## 7. C API 分层契约

### 7.1 全局原则

- 依赖方向固定为 `BSP/HAL -> Driver -> Service -> Control -> Application`。
- Control 和应用层只接收显式快照，不读取 GPIO/I2C/UART 寄存器。
- 所有采样结构都含 `sample_time_ms` 和 `valid`；时间基准来自 BSP 的 1 ms tick。
- 不使用 RTOS、动态内存、递归或运行中 `malloc`。驱动、服务和显示缓冲在编译期分配。
- ISR 只做计数、捕获时间或置位；不执行 PID、状态机、OLED 刷新、字符串格式化或长日志。
- BSP 上电初始化时可以把 PWM、方向和 STBY 配置为安全静态电平；进入正常运行后，PWM、方向和 STBY 只能由 TB6612 Driver 经 BSP 原语写入。应用层是唯一允许提交执行器请求的模块。
- 外部 OLED 和 HC-05 只能读取 `sc_app_snapshot_t`，不能通过写接口反改控制状态。
- 速度、距离、角度、时间和 PWM 在跨层接口中保持统一单位，不做隐式单位换算。

### 7.2 公共类型

以下为接口契约示意，不是可编译实现代码：

```c
typedef enum {
    SC_OK = 0,
    SC_ERR_ARGUMENT,
    SC_ERR_NOT_READY,
    SC_ERR_TIMEOUT,
    SC_ERR_BUS,
    SC_ERR_INVALID_DATA,
    SC_ERR_CALIBRATION_REQUIRED,
    SC_ERR_HARDWARE,
    SC_ERR_BUFFER_FULL,
    SC_ERR_NOT_SUPPORTED
} sc_status_t;

typedef enum {
    SC_SIDE_LEFT = 0,
    SC_SIDE_RIGHT = 1
} sc_side_t;

typedef struct {
    uint8_t raw_levels;       /* 低四位，bit0=O1..bit3=O4；0=黑，1=白 */
    uint32_t sample_time_ms;
    uint32_t sequence;
    uint8_t valid;
} sc_line_sample_t;

typedef enum {
    SC_LINE_INVALID = 0,
    SC_LINE_STRAIGHT,
    SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE,    /* 左直角弯候选 */
    SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE,   /* 右直角弯候选 */
    SC_LINE_LEFT_LARGE_BEND_CANDIDATE,     /* 左大弯候选 */
    SC_LINE_RIGHT_LARGE_BEND_CANDIDATE,    /* 右大弯候选 */
    SC_LINE_LEFT_FINE_ADJUST_CANDIDATE,    /* 左微调候选 */
    SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE,   /* 右微调候选 */
    SC_LINE_JUNCTION_CANDIDATE,
    SC_LINE_LOST_CANDIDATE,
    SC_LINE_UNKNOWN_PATTERN
} sc_line_class_t;

typedef struct {
    sc_line_sample_t sample;
    uint8_t black_mask;       /* 低四位，bit0=O1..bit3=O4；检测到黑为1 */
    sc_line_class_t semantic_state;
} sc_line_state_t;

typedef struct {
    int64_t left_total_count;
    int64_t right_total_count;
    int64_t left_window_delta_count;
    int64_t right_window_delta_count;
    uint16_t left_raw_count;
    uint16_t right_raw_count;
    int32_t left_speed_mm_s;
    int32_t right_speed_mm_s;
    uint32_t sample_time_ms;
    uint32_t sequence;
    uint32_t left_window_span_ms;
    uint32_t right_window_span_ms;
    uint8_t left_valid;
    uint8_t right_valid;
    uint8_t valid;
} sc_wheel_feedback_t;

typedef struct {
    int32_t gyro_z_mdps;          /* Driver 为测量值；Service 输出时扣除零偏 */
    int32_t heading_delta_mdeg;   /* Driver 固定置 0；仅 Service 填写 */
    uint32_t sample_time_ms;
    uint8_t calibrated;
    uint8_t valid;
} sc_imu_sample_t;

typedef struct {
    sc_wheel_calibration_t wheel_calibration;
    sc_speed_controller_config_t speed_controller;
    int32_t cruise_mm_s;
    uint8_t calibration_valid;
    uint8_t tuning_valid;
    int8_t left_motor_sign;
    int8_t right_motor_sign;
} sc_app_config_t;

typedef struct {
    int32_t left_mm_s;
    int32_t right_mm_s;
} sc_wheel_target_t;

typedef enum {
    SC_APP_PHASE_BOOT = 0,
    SC_APP_PHASE_IDLE,
    SC_APP_PHASE_READY,
    SC_APP_PHASE_ACTIVE,
    SC_APP_PHASE_STOPPING,
    SC_APP_PHASE_FAULT
} sc_app_phase_t;

typedef struct {
    sc_app_phase_t phase;
    sc_line_state_t line;
    sc_encoder_snapshot_t encoder;
    sc_wheel_feedback_t wheel;
    sc_wheel_target_t target;
    sc_speed_controller_output_t speed_output;
    int16_t left_pwm;             /* 经电机符号换算后的实际提交值 */
    int16_t right_pwm;
    uint32_t fault_bits;
    uint32_t degraded_bits;
    sc_app_stop_reason_t stop_reason;
    uint8_t software_stby;
    uint8_t calibration_valid;
    uint8_t tuning_valid;
    uint32_t sequence;
    uint32_t time_ms;
} sc_app_snapshot_t;
```

统一单位和取值范围：

| 量 | 单位 | 约定 |
|---|---|---|
| 时间戳 | `ms` | BSP 1 ms tick；控制周期使用差分时间。 |
| 距离 | `mm` | 沿车体纵轴累计的轮里程。 |
| 速度 | `mm/s` | 有符号，前进为正。 |
| 角度 | `mdeg` | 短时航向增量，逆时针为正。 |
| 角速度 | `mdeg/s` | gyro-Z，逆时针为正。 |
| PWM 权限 | `-1000..1000` | `0` 为零输出，绝对值 1000 为最大允许占空比。 |
| 编码器计数 | `count` | 硬件定时器计数与方向变化量。 |
| 有效性 | `uint8_t` | `1` 有效，`0` 不可参与控制。 |

`sc_status_t` 的错误职责：参数错误、未就绪、超时、总线错误、无效数据、校准缺失和硬件故障必须能分别上报。可选设备的 NACK 用超时或总线错误表示，不能把所有错误压成一个泛化失败码。

未来持久化配置记录固定使用 `magic=0x53434647`（ASCII `SCFG`）、`schema_version=1`、`payload_size=sizeof(sc_config_t)`，CRC 使用 CRC-32/ISO-HDLC 并覆盖从 `magic` 到 `crc32` 之前的全部字节。写入前先将整个结构体清零，避免 CRC 覆盖未初始化的填充字节。`calibration_valid=0` 时配置可以被读取，但应用启动门必须拒绝发车；标定通过后置为 1。本阶段尚未实现 Flash 读写，配置页规划固定为 STM32F103C8T6 的最后一个 1 KB Flash 页，末页起始地址为 `0x0800FC00`；未来链接脚本必须保留该页。

`sc_imu_sample_t` 采用同一类型的两个阶段：`sc_mpu6050_sample()` 返回的原始阶段包含测量角速度，`heading_delta_mdeg` 固定为 0，`calibrated=1` 只表示零偏数据可用；`sc_imu_service_update()` 返回处理阶段，负责零偏修正并填写短期 `heading_delta_mdeg`。Control 只接收处理阶段。

### 7.3 BSP/HAL

| API | 输入 | 返回/输出 | 责任边界 |
|---|---|---|---|
| `sc_bsp_init()` | 无 | `sc_status_t` | 初始化时钟、GPIO、TIM2/TIM3 编码器、TIM4 PWM、I2C2、USART2、Flash；上电阶段把 PWM=0、方向脚低、STBY 低配置为安全静态电平。 |
| `sc_bsp_millis()` | 无 | `uint32_t` | 1 ms 时间基准，只读。 |
| `sc_bsp_gpio_read(pin)` | 固定引脚编号 | 电平 | 只供 Button 和 LF04 Driver 读取。 |
| `sc_bsp_gpio_write(pin, level)` | 固定引脚编号、电平 | `sc_status_t` | 上电初始化可设置安全静态电平；运行期只供 TB6612、LED Driver 写入，应用和 Control 不直接调用。 |
| `sc_bsp_pwm_set(channel, duty_permille)` | 通道、`0..1000` | `sc_status_t` | 上电初始化可设零占空比；运行期只供 TB6612 Driver 写 TIM4 CH1/CH2。 |
| `sc_bsp_encoder_read(side)` | 左/右 | `int32_t` 扩展累计计数 | 把 16 位定时器扩展成 32 位计数；只返回计数，不解释速度。 |
| `sc_bsp_encoder_reset(side)` | 左/右 | `sc_status_t` | 清编码器累计值。 |
| `sc_bsp_i2c_write(addr, buf, len, timeout_ms)` | 7 位地址、数据、超时 | `sc_status_t` | 阻塞上限由调用方给出；不作为控制循环隐式等待。 |
| `sc_bsp_i2c_read(addr, reg, buf, len, timeout_ms)` | 地址、寄存器、缓冲、超时 | `sc_status_t` | 读失败必须带错误状态。 |
| `sc_bsp_uart_write(port, buf, len)` | 串口号、数据 | `sc_status_t` | 写入固定大小发送缓冲；空间不足返回 `SC_ERR_BUFFER_FULL`，不等待远端正响应。 |
| `sc_bsp_uart_read(port, buf, len)` | 串口号、缓冲 | `(status, 实际字节数)` | 轮询读取，不阻塞控制周期。 |
| `sc_bsp_flash_read(offset, buf, len)` | 页/偏移、缓冲 | `sc_status_t` | 只读校验和读取。 |
| `sc_bsp_flash_write(offset, buf, len)` | 页/偏移、写缓冲 | `sc_status_t` | 只由 Config Service 在 Application 已确认静止后调用，并做地址范围检查。 |
| `sc_bsp_flash_erase_page(page)` | 页号 | `sc_status_t` | 只由 Config Service 在 Application 已确认静止后调用，并做地址范围检查。 |

BSP 只提供原语，不决定循迹行为。编码器 TIM2/TIM3 和 UART/I2C 超时值是硬件访问参数，不放入控制算法参数。

### 7.4 Driver 层

| API | 输入 | 返回/输出 | 责任边界 |
|---|---|---|---|
| `sc_lf04_init()` | 当前 BSP | `sc_status_t` | 配置 `PB12-PB15` 为输入，读回四路初始电平。 |
| `sc_lf04_sample()` | 当前时间 | `sc_line_sample_t` | 把四路原电平装成 `O1..O4`；不分类、不滤波、不做转弯。 |
| `sc_tb6612_init()` | 当前 BSP | `sc_status_t` | 拉低 STBY、方向脚和两路 PWM；确认无效输出。 |
| `sc_tb6612_set_standby(enable)` | `uint8_t` | `sc_status_t` | 只改变 STBY；该函数是唯一 STBY 写入口。 |
| `sc_tb6612_apply(left_permille, right_permille)` | 两路有符号 PWM 权限 | `sc_status_t` | 把有符号命令转换成正/反/制动/停转和两路方向；越界拒绝。 |
| `sc_safety_emergency_stop()` | 无 | 无 | 直接拉低 PA4/PA5、PB0/PB1、PB8 并清零 TIM4 CCR1/CCR2；不依赖 HAL 句柄或驱动状态，供 Error_Handler 和 CPU 异常入口首先调用。 |
| `sc_safety_iwdg_init()` / `sc_safety_iwdg_feed()` | 无 | `sc_safety_status_t` | 先启动 LSI，再配置 PR/RLR 并有界等待更新；超时返回错误。feed 只重装计数，不改变配置。 |
| `sc_encoder_init()` | 当前 BSP | `sc_status_t` | 启动 TIM2/TIM3 编码器模式并清零累计。 |
| `sc_encoder_sample()` | BSP 计数、时间 | `sc_wheel_feedback_t` | 输出计数差和有效性，不直接读控制状态。 |
| `sc_mpu6050_init()` | I2C2 地址 `0x68` | `sc_status_t` | 检查 WHO_AM_I、配置量程和数字滤波；不假装永久航向。 |
| `sc_mpu6050_calibrate()` | 静止样本窗口 | `sc_status_t` | 产生并返回 gyro-Z 零偏。 |
| `sc_mpu6050_sample()` | 当前时间 | `sc_imu_sample_t` | 输出测量角速度、校准与有效性；`heading_delta_mdeg` 固定为 0，不在驱动内积分。 |
| `sc_ssd1306_init()` | I2C2 地址 `0x3C` | `sc_status_t` | 初始化显示控制器；NACK 只影响显示。 |
| `sc_ssd1306_write_line(row, text)` / `sc_ssd1306_refresh()` / `sc_ssd1306_service(now_ms)` | 固定行文本、时间 | `sc_ssd1306_status_t` | OLED 只显示 `sc_telemetry` 生成的只读快照文本；刷新异步推进，失败不修改控制状态。 |
| `sc_uart_init(baud, rx_enable)` | 波特率、接收使能 | `sc_status_t` | 固定 HC-05/USART2 9600 8N1；正式固件传 `0`，Bench 传 `1`。 |
| `sc_uart_write(text)` | 只读文本 | `sc_status_t` | 只发送遥测或诊断行，不等待远端。 |
| `sc_button_init()` | 当前 BSP | `sc_status_t` | 配置 `PB5` 上拉输入。 |
| `sc_button_poll(now_ms)` | 时间 | `uint8_t` 事件位 | 只输出按下/释放/长按事件，不直接启动电机。 |
| `sc_led_init()` / `sc_led_set(on)` | 当前 BSP | `sc_status_t` | 只控制 `PB9` 状态灯。 |

Driver 层不得调用 Service、Control 或 Application。`sc_tb6612_apply()` 不负责安全判断；任何越界、未初始化或故障状态都必须返回错误而不写半有效输出。

### 7.5 Service 层

| API | 输入 | 返回/输出 | 责任边界 |
|---|---|---|---|
| `sc_line_service_update(sample)` | `sc_line_sample_t` | `sc_line_state_t` | 计算黑线掩码并分类；只解释 LF04 位型。 |
| `sc_wheel_service_update(service, snapshot, calibration, now_ms, feedback)` | 单个编码器快照、标定、时间 | `sc_wheel_feedback_t` | 每侧保存最近 6 个快照，以最近 5 个有效时间差计算首末累计计数差；热身后每个新快照都输出左右独立有效性和有符号 `mm/s`。 |
| `sc_pid_calculate()` / `sc_pid_commit()` | 目标、测量、实际 `dt_ms`、最终提交值 | 未限幅 PID 输出、条件积分提交 | 并联 PID，微分作用测量值；根据限幅和斜坡后实际提交值冻结积分。 |
| `sc_speed_controller_step(left_ref_mm_s, right_ref_mm_s, feedback, now_ms)` | 两轮目标、Wheel 反馈、时间 | `(status, left/right PWM permille)` | 目标斜坡、前馈、左右独立 PID、输出斜坡和 `-564..564` 限幅；任一侧反馈无效时两路清零。 |
| `sc_odometry_service_update(feedback)` | 轮反馈 | 左右累计距离 `mm` | 只做轮里程积分，不用来声称四轮状态。 |
| `sc_imu_service_update(raw_imu, dt_ms)` | Driver 原始 `sc_imu_sample_t`、时间差 | 处理后的 `sc_imu_sample_t` | 负责零偏修正并填写短期 `heading_delta_mdeg`；超时/无效时保持零增量并降级。 |
| `sc_config_service_load(config_out)` | Flash 配置页 | `sc_status_t` + 配置 | 校验 magic、版本、长度和 CRC32；失败返回 `SC_ERR_CALIBRATION_REQUIRED`。 |
| `sc_config_service_save(config_in)` | 完整配置、Application 静止确认 | `sc_status_t` | 只在 Application 已确认静止后，先算 CRC 再调用 BSP Flash 原语。 |
| `sc_telemetry_init(now_ms)` | 当前时间 | 无 | 设置 5 Hz UART 与 2 Hz OLED 调度基线；缓冲全部为静态存储。 |
| `sc_telemetry_service(snapshot, now_ms, degraded_bits_out)` | 只读 App 快照、时间 | `sc_telemetry_status_t` + 新增降级位 | 更新 LED/OLED；执行 5 Hz 单行 CSV 整帧入队。UART/显示失败只返回并累计对应降级位，不等待无线连接。 |
| `sc_scheduler_service_tick(now_ms)` | 当前时间 | 触发位 | 产生固定周期任务标志；ISR 不执行业务。 |

编码器换算是 Service 的职责，不允许在 Control 或 Application 中重复实现。`sc_speed_controller_step()` 只回答“给定目标和反馈，控制请求是什么”，不直接写 TB6612。

### 7.6 Control 层

| API | 输入 | 返回/输出 | 责任边界 |
|---|---|---|---|
| `sc_track_controller_init(controller, cruise_mm_s)` | 控制器、巡航速度 | 无 | 清除方向、丢线和重捕状态；`cruise_mm_s` 作为比例策略基准。 |
| `sc_track_start_eligible(line_state)` | 最新线路状态 | `uint8_t` | 只有 `sample.valid!=0` 且 `black_mask==0x06` 返回 1。 |
| `sc_track_controller_step(controller, line_state, now_ms, target_out)` | 控制器、线型、时间 | `(status, sc_wheel_target_t)` | 输出左右有符号 `mm/s`；不写 PWM，不访问 TB6612。 |
| `sc_motion_controller_step(left_ref_mm_s, right_ref_mm_s, wheel_feedback, dt_ms)` | 目标、反馈 | `(status, [left_pwm, right_pwm])` | 调用服务层的双轮速度控制入口，输出归一化 PWM 请求。 |
| `sc_motion_controller_stop()` | 无 | `sc_status_t` | 清除控制器积分状态并返回零请求；不直接拉 STBY。 |

循迹策略以 `cruise_mm_s` 乘以下表整数比例并四舍五入：

| 状态 | 左比例‰ | 右比例‰ |
|---|---:|---:|
| 中心/十字 | 1000 | 1000 |
| 微调左 | 558 | 1328 |
| 微调右 | 1328 | 558 |
| 中弯左 | 268 | 1386 |
| 中弯右 | 1386 | 268 |
| 大弯左 | 0 | 1424 |
| 大弯右 | 1424 | 0 |
| 向左找线 | 76 | 1194 |
| 向右找线 | 1194 | 76 |

所有目标 clamp 到 `0..min(INT32_MAX, 3*cruise_mm_s)`。中心、十字和左右对称歧义样本清除方向历史。全白且方向历史为 0 时输出 0 并返回 `SC_TRACK_ERR_LOST`。方向已知时锁存首次丢线时间。重捕候选要求连续 3 个 sequence 不同、相邻样本时差为 1..30 ms 的有效非零样本且首末至少 20 ms；白帧、无效帧、重复 sequence、零时差或超过 30 ms 间隔均清候选。上述任一过程都不重置首次丢线的 1000 ms 总期限。

### 7.7 Application 层

| API | 输入 | 返回/输出 | 责任边界 |
|---|---|---|---|
| `sc_app_get_default_config(config)` | 配置缓冲 | 无 | 从 `sc_motor_defaults.h` 装载轮径/计数、符号、巡航速度和 PID 占位值；两个 VALID 当前均为 0。 |
| `sc_app_init(app, config, now_ms)` | App、配置、时间 | `sc_app_status_t` | 初始化核心 Driver/Wheel/PID/速度控制，进入 BOOT 后固定落到 IDLE；核心初始化或结构校验失败进入 FATAL FAULT 并紧急停机。 |
| `sc_app_service(app, now_ms)` | App、时间 | `sc_app_status_t` | 执行 10 ms 线路采样、编码器 Service、新反馈速度控制、按键状态机、故障检测、恢复健康和唯一 TB6612 提交。 |
| `sc_app_snapshot(app, snapshot)` | App、调用方缓冲 | `sc_app_status_t` | 复制只读快照：阶段、线路、编码器/Wheel、目标、控制器输出、经电机符号换算的 PWM、故障/降级、STBY、序号和时间。 |
| `sc_app_report_degraded(app, bits)` | IMU/显示/遥测降级位 | `sc_app_status_t` | 只累计可选设备降级，不改变电机控制或安全门。 |

正式固件没有公开的 `arm/start/commit_actuator` 控制入口；发车只由 `sc_app_service()` 内完整按键序列触发。`sc_app` 是正式执行路径唯一调用 `sc_tb6612_apply()` 与 `sc_tb6612_set_standby()` 的模块；TB6612 Driver 仍是唯一寄存器写入者。Bench 是独立构建，不进入 Debug/Release ELF。

### 7.8 运行状态与故障位

状态固定为 `BOOT/IDLE/READY/ACTIVE/STOPPING/FAULT`。正式快照使用 `fault_bits`、`degraded_bits` 和 `stop_reason`：

| 故障位 | 触发条件 | 对外表现 |
|---|---|---|
| `SC_APP_FAULT_LINE_INVALID` | LF04 无效、线路样本无效或 Track 参数无效 | 可恢复 FAULT，零输出并拉低 STBY。 |
| `SC_APP_FAULT_LINE_LOST` | Track 确认方向未知全白或 1000 ms 找回超时 | 可恢复 FAULT。 |
| `SC_APP_FAULT_FEEDBACK` | Wheel 反馈无效或最近有效反馈超过 30 ms | 可恢复 FAULT。 |
| `SC_APP_FAULT_CONTROL` | 速度/目标控制返回错误 | 可恢复 FAULT。 |
| `SC_APP_FAULT_SCHEDULE` | 相邻应用循环超过 100 ms | 可恢复 FAULT。 |
| `SC_APP_FAULT_STALL_LEFT/RIGHT` | 该侧目标至少 50 mm/s、已提交 PWM 至少 150；300 ms 固定窗口的物理净进展低于按目标/时间/每转计数/轮径计算理论值的 20% | 对应轮故障锁存至健康窗口，停车；单帧正计数不重置窗口。 |
| `SC_APP_FAULT_DIRECTION_LEFT/RIGHT` | 正目标下连续 3 个新 sequence 物理计数反向 | FATAL，按键不可清除。 |
| `SC_APP_FAULT_TB6612` | TB6612 初始化、STBY/PWM 提交或状态回读失败 | FATAL。 |
| `SC_APP_FAULT_HARDWARE` | 编码器 HAL/配置/未就绪、TB GPIO 状态异常等核心硬件错误 | FATAL。 |
| `SC_APP_FAULT_CONFIG` | App、Track、PID 参数结构无效 | FATAL。 |
| `SC_APP_FAULT_INIT` | 核心初始化阶段失败 | FATAL。 |

可恢复 FAULT 只有在当前配置有效、线路样本有效且非白、反馈新鲜、TB6612 处于安全待机时连续健康 200 ms 才清除故障位并回到 IDLE/READY。若含 `LINE_LOST`，还必须重新看到中心 `0x06` 并完成稳定窗口。不会自动重新运动；下一次运动必须由新的完整 PRESSED -> RELEASED 短按触发。方向故障只有在目标与控制器命令同号且物理计数反向时才累计；目标为正但控制器输出负 PWM 时不单独判方向 FATAL。FATAL 只能断电重启；IWDG 或外部复位后固定从 BOOT 初始化到 IDLE。

`degraded_bits` 单独记录 `IMU/DISPLAY/TELEMETRY`。MPU、OLED、HC-05 的初始化、刷新或入队失败只置对应降级位，不改变目标、PWM 或 STBY。OLED 异步 Service 后还必须读取状态和重试计数，不能只依赖 `write_line/refresh` 的同步返回。

持久化配置结构未来必须包含 `magic`、`schema_version`、`payload_size` 和 `crc32`，但本阶段尚未实现 Flash Config Service。当前正式配置来源是 `sc_motor_defaults.h` 编译期常量；任何实际发车前都必须完成标定和调参并把两个 VALID 显式置 1。

本基线没有为电池电压分配 ADC 通道，因此不伪造“电池电压在线监测”能力。电源欠压由硬件复位和断电表现，软件重新启动后回到待命；上电前电压、极性和温升属于台架检查项。

## 8. 标定与调试接口

### 8.1 按键和基础运行入口

- `PB5` 配置为上拉输入，按下为低。短按释放只向 `sc_app_service()` 内部的启动门提出请求，不直接写 PWM。
- 初始化时按键稳定态直接取当前原始电平；上电已按住时不会伪造新的 `PRESSED`。释放事件可能没有前置 `PRESSED`，应用必须按 `PRESSED -> RELEASED` 序列配对并忽略孤立的 `RELEASED`。
- 长按在任意状态立即安全停机；长按对应的释放不能重新启动。新的运动必须等待下一次完整短按。
- 发车还要求 `calibration_valid && tuning_valid`、双轮 Wheel Service 有效，以及最近约 50 ms 内 6 个 `sequence` 不同、间隔不大于 30 ms 的中心 `black_mask=0x06` 样本。全白、线路无效、反馈陈旧或配置无效时保持 IDLE 且不置高 STBY。
- 按键消抖、长按判定、状态迁移和恢复动作都由主循环处理，不写在 GPIO 中断里。
- OLED 和 LED 只消费应用快照，不参与控制。

### 8.2 HC-05 Bench 台架命令

独立 Bench 固件通过 USART2/HC-05 的 `9600 8N1` 接收受控命令，正式固件不链接命令层且不解析入站运动命令。当前 Bench 命令集合为：

| 命令 | 作用 | 限制 |
|---|---|---|
| `PING` | 验证链路 | 只返回 `OK PONG`。 |
| `STATUS` | 输出 TB6612 与编码器状态 | 只读。 |
| `LINE` / `LF` | 输出 LF04 原始电平、掩码和线路语义 | 只读。 |
| `ARM` | 打开 5 秒布防窗口 | 电机必须已处于安全待机。 |
| `MOTOR <L|R> <±150> <100-2000>` | 单轮限时测试 | 必须布防，每次命令只执行一个受限窗口。 |
| `STOP` | 零输出并撤销 STBY | 任意时刻安全停止。 |
| `RESET` | 停止状态下重置编码器计数 | 运行或布防状态拒绝。 |
| `HELP` | 输出命令列表 | 只读。 |

Bench 固件同样依赖 STBY 安全序列和超时自动停机；它不进入 Debug/Release 正式产物。

### 8.3 HC-05 遥测

正式 USART2 固定 `9600 8N1`，`rx_enable=0`，只发送只读遥测；Bench 才以 `rx_enable=1` 接管入站命令。正式遥测以 5 Hz（200 ms）最多发送一行紧凑 ASCII CSV：

```text
time_ms,phase,raw_levels,black_mask,target_left_mm_s,target_right_mm_s,
measured_left_mm_s,measured_right_mm_s,left_pwm,right_pwm,
stby,fault_bits,degraded_bits
```

每帧在静态缓冲中一次构建并一次入队；完整帧构建失败或发送队列拒绝时整帧丢弃并置 `SC_APP_DEGRADED_TELEMETRY`。成功只表示整帧进入本地发送队列，不表示远端蓝牙已连接或已收到。正式固件不调用 `sc_uart_read()`，也不解析入站电机速度、PWM、方向、引脚或 PID 命令；Release ELF 不含 Bench 命令层。蓝牙断开、配对失败或线路错误只标记遥测降级。

### 8.4 OLED

OLED 使用 I2C2 `0x3C`，以 2 Hz（500 ms）显示快照而非可写控制入口：阶段、原始线路/黑线掩码、左右目标、左右实测 `mm/s`、左右实际 PWM、STBY、停止原因和故障位。调用异步 Service 后读取 `sc_ssd1306_get_status()` 的 `last_status`、状态和重试计数；同步或异步失败只设置 `SC_APP_DEGRADED_DISPLAY`，不得阻塞控制或改变电机输出。

状态 LED 为：IDLE 灭、READY 500 ms 翻转慢闪、ACTIVE 常亮、FAULT 100 ms 翻转快闪；BOOT/STOPPING 灭。

IWDG 初始化先写 `0xCCCC` 启动 IWDG/LSI，再写 `0x5555` 解锁并配置 PR/RLR；PVU/RVU 更新等待有固定上限，超时返回错误。正式和 Bench 均检查初始化结果，失败进入紧急停机和 `Error_Handler`。LSI 名义 40 kHz、预分频 `/64`、重装 312，名义期限 `0.4992 s`，实际期限取决于 LSI 容差。主循环在核心采样、状态推进、安全提交和必要 Service 之后再喂狗。受控 App FAULT 循环继续喂狗，不会自动重启；MCU 复位后重新进入 BOOT/IDLE，不恢复上一次运动。

### 8.5 配置保存与恢复

- 配置使用 Flash 固定页区，字段带 magic、版本、长度和 CRC32。
- 只有 Application 已确认处于静止台架状态后，Config Service 才允许调用 Flash 擦除和写入原语。
- 未来写入或读回失败必须进入配置 FATAL，下一次上电也必须读取到有效记录才能发车；当前尚未实现 Flash Config Service。
- 默认恢复把 LF04 位序/极性、电机和编码器方向、轮径/计数和 IMU 零偏恢复为名义值，并令 `calibration_valid=0`；开始运动前必须重新完成方向与轮径标定。该操作不写入 PID 增益或找回时长。
- 名义恢复值为：LF04 不倒序、不反相，左右电机和编码器符号均为 `+1`，左右计数均为 1040 count/rev，轮径为 48000 μm，gyro-Z 零偏为 0。
- 保存后立即回读 magic、版本、长度、字段和 CRC，显示或串口输出成功状态。

### 8.6 现场标定判据

| 项目 | 操作 | 通过判据 |
|---|---|---|
| LF04 黑白与左右顺序 | 探头离地约 3 cm，先放白底，再逐路放在黑线上；按车体左到右记录 `O1..O4`。 | 白底为 `1`，逐路黑线为 `0`；调换左右顺序后分类应改变，说明顺序被正确识别。 |
| LF04 十字与丢线 | 将四路同时盖黑、同时置于白底，读取原始位型。 | 得到 `0000` 与 `1111` 两个不同原状态，分类分别为全黑十字候选与全白丢线候选。 |
| 编码器供电与相序 | 用万用表确认编码器 VCC=3.3V；抬轮缓慢正向前进，观察 A/B。 | A/B 峰值为 3.3V；正转时按配置方向计数递增，A/B 交换后结果应改变。 |
| 轮径与计数 | 名义 1040 count/rev、48 mm 轮径；人工转 10 圈并测直线距离。 | 实测距离与名义 1508 mm 的偏差不超过 2%；超出时更新每转计数或轮径标定。 |
| 左右电机正方向 | 抬轮，应用只给正目标速度。 | 两轮均向车体前方运动；任一方向相反，必须通过线束/软件方向配置纠正后再进入闭环。 |
| TB6612 左右通道 | 单独给左、右轮小 PWM，保持 STBY 高。 | 左请求只带动左轮，右请求只带动右轮；不得出现左右错位或两轮同时意外动作。 |
| MPU6050 零偏 | 车辆静止，采样窗口完成 bias，再左右旋转车体。 | 静止时角速度接近零，旋转方向符号正确；曲线只用于短时辅助。 |
| I2C 地址 | 在 I2C2 扫描地址。 | MPU6050 出现 `0x68`，OLED 出现 `0x3C`；单个设备缺失只降级对应功能。 |
| 电源与共地 | 断电检查电池、5V、3.3V 极性和共地，再依次上电。 | 无 5V 进入编码器/I2C；输入端电压在 3.3V 允许范围；Logic GND、PGND 和电池负极连通。 |
| 单电源模式 | 检查电池、Type-C 与 ST-Link 的连接状态。 | 运行只用电池；烧录只用 ST-Link 3.3V；不出现两个电源同时向目标供电。 |

## 9. 实现验收矩阵与缺陷对照

本矩阵同时作为验收规范和当前状态边界。驱动、语义与 Bench 对应的主机断言和缺陷变体已经执行；涉及实物缺失/拔插、完整电源和整车的条目仍需台架验证。每行都需要在“正确参考版本”和“隔离缺陷版本”之间做同一目标断言的双向对照：正确参考预期通过，缺陷版本目标断言失败。不得根据相互独立的边界用例替代目标对照。

| 编号 | 场景 | 正确参考版本的目标断言 | 隔离缺陷版本与预期失败 |
|---|---|---|---|
| A01 | LF04 白底四路原电平 | `raw_levels=1111`，分类为 `SC_LINE_LOST_CANDIDATE`，不判十字 | 把电平取反或把 `1111` 归一化为全黑，断言失败。 |
| A02 | LF04 四路同时压黑 | `raw_levels=0000`，`black_mask=0x0F`，分类为 `SC_LINE_JUNCTION_CANDIDATE` | 把 `0000` 同时映射为丢线，分类断言失败。 |
| A03 | LF04 左右顺序 | `O1` 为车体最左、`O4` 为最右；`0001` 产生 `SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE` | 交换 O1/O4 后应变为 `SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE`；若仍产生左直角枚举，断言失败。 |
| A04 | LF04 典型直线 | `1001` 产生 `SC_LINE_STRAIGHT`，掩码为 `0x06` | 用未取反 raw 当 black_mask，掩码/分类断言失败。 |
| A05 | LF04 普通左/右微调 | `1011` 产生 `SC_LINE_LEFT_FINE_ADJUST_CANDIDATE`，`1101` 产生 `SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE` | 将两类微调合并为泛化弯道枚举，或不分左右，接口层断言失败。 |
| A06 | 编码器 A/B 反接 | 正转时按配置 sign 得到正轮速，`valid=1` | 交换 A/B，方向/符号断言失败；不能以计数仍在变化作为通过。 |
| A07 | 编码器目标与实际方向 | 左/右正目标分别产生正 `mm/s` | 任一轮编码器符号反向，速度闭环方向断言失败。 |
| A08 | TB6612 左右通道 | 左 PWM 只驱动左轮，右 PWM 只驱动右轮 | 交换 A/B 输出的轮端线或交换 IN 通道，左右通道断言失败。 |
| A09 | 电机前进方向 | 正目标命令下两轮朝车体前方 | 任一轮方向接反，车辆方向断言失败。 |
| A10 | 单块 TB6612/单块核心板 | 主链路只使用 1 个 TB6612 和 1 个 STM32 | 同时依赖第二块板或第二驱动才能完成测试，主链独立性断言失败。 |
| A11 | 连续十字 | 连续 `0000` 样本保持十字候选，直行策略不被普通弯道状态覆盖 | 把 `0000` 当作丢线或普通弯道，十字分类/直行请求断言失败。 |
| A12 | 短时全白 | 事件先产生 `SC_LINE_LOST_CANDIDATE`，保留有效性供策略处理 | 把首次全白直接等同 `0000` 十字，分类断言失败。 |
| A13 | MPU6050 缺失 | 核心 LF04/编码器/TB6612 仍初始化，IMU 降级位置位 | 因 I2C 设备缺失阻塞整个初始化，核心可运行断言失败。 |
| A14 | OLED 缺失 | `SC_APP_DEGRADED_DISPLAY`，控制快照继续更新 | 因 OLED NACK 把 TB6612 或服务循环锁死，断言失败。 |
| A15 | HC-05 缺失/断连 | `SC_APP_DEGRADED_TELEMETRY`，CPU 不等待无线 ACK，执行器仍受应用安全门控制 | 因蓝牙断开阻塞控制、停车，或让入站字符改变输出，断言失败。 |
| A16 | I2C NACK | 返回 `SC_ERR_TIMEOUT`/`SC_ERR_BUS`，不伪造有效样本 | 把 NACK 当作零角速度或有效显示数据，断言失败。 |
| A17 | 目标板电源单源 | 电池、Type-C、ST-Link 3.3V 中只允许一个向目标板供电 | 两个源同时接目标板 5V/3.3V 电源轨仍允许上电，电源检查断言失败。 |
| A18 | 极性/共地 | 编码器只接 3.3V，I2C 上拉只回 3.3V，PGND/GND 和电池负极共地 | 接 5V 编码器、5V 上拉或悬空功率地，电气检查断言失败。 |
| A19 | 辅助通道隔离 | 断开 MPU、OLED、HC-05 中任一设备，基本启动入口和执行器安全门仍可初始化 | 任一辅助设备成为控制必要条件，降级断言失败。 |
| A20 | 配置 CRC/版本 | 有效配置能载入；篡改任一字节或版本后返回校准缺失 | CRC 被忽略或旧版本被直接接受，断言失败。 |
| A21 | API 依赖 | 静态搜索或链接依赖显示 BSP→Driver→Service→Control→Application | Driver 反向调用 Service 或 Control 直接引用 GPIO/UART，断言失败。 |
| A22 | 满电与负载供电 | 8.4V 电池输入下完成左右轮交替加减速，核心板不复位；5V 轨保持 4.75-5.25V，3.3V 轨保持 3.17-3.43V，TB6612 和 AMS1117 不触发热保护 | 功率地与逻辑地错误共用回流、稳压输出不足或输入跌落后仍宣称通过，断言失败。 |
| A23 | LF04 安装几何 | 传感器前缘对地约 3 cm，四路均无机械遮挡，车体左右基准与 O1/O4 一致 | 高度明显偏大、支架倾斜或 O1/O4 反装，黑白识别/左右顺序断言失败。 |

验收证据必须同时包含：正确参考版本通过结果、对应缺陷版本失败结果、测试输入/夹具隔离方式和实际运行时间。主机测试结果已记录于工程状态；正文未列明完成台架记录的项目不产生“实物通过”结论。

## 10. 来源清单

以下文件均在本地资料目录或现有工程目录中存在，本文只使用其文件名作为来源标识，不提供外部网络依赖。

| 类别 | 本地文件或资料 |
|---|---|
| 物料数量与序号 | `C:\Users\RedApple\Desktop\竞赛\循迹小车\硬件清单.xlsx`，`Sheet1` 第 1-45 行，数据项 1-44。 |
| 赛道参考 | `C:\Users\RedApple\Desktop\竞赛\循迹小车\赛道图片.jpg`。 |
| LF04 使用与真值表 | `LF04四路红外巡线模块使用手册_2026.5.19.pdf`。 |
| LF04 原理与示例 | `四路红外巡线模块原理图.pdf`；`STM32F103C8T6模块数据采集例程 .zip`；`STM32F103RCT6_通用巡线例程_C10B.zip`。 |
| MG310 电机参数 | `轮趣直流有刷电机产品说明手册（尺寸、参数、线序）_2026.07.28.pdf`；`直流电机开发手册_2026.07.28.pdf`。 |
| 编码器接线实测资料 | `编码器电源说明.png`；实际模块引脚图中的 `IMG_20261108_225541.jpg`。 |
| TB6612 IC 参数 | `TB6612FNG_datasheet_zh_cn_20141001.pdf`。 |
| TB6612 模块原理图 | `TB6612FNG模块原理图（D103A）.pdf`；`IMG_20261108_225258.jpg`。 |
| STM32F103C8T6 数据手册 | `STM32F103x8B_DS_CH_V10.pdf`。 |
| STM32 核心板原理图 | `STM32F103C8T6-TYPEC-原理图.pdf`；`STM32F103C8T6最小系统板原理图.pdf`；`IMG_20261108_225032.jpg`。 |
| 现有工程 | `C:\Users\RedApple\Documents\smart-car\Smart-Car.ioc`；`CMakeLists.txt`；`CMakePresets.json`；`README.md`。 |
| MPU6050 模块资料 | `5.MPU6050模块手册-.pdf`；`7.MPU6050模块_原理图.pdf`。 |
| HC-05 模块资料 | `HC-05 用户手册.pdf`；`HC-05蓝牙串口模块使用规格书(含指令集）.pdf`；`IMG_20261108_225210.jpg`。 |
| OLED 资料与实际引脚 | `5.0.96OLED显示屏_数据手册.pdf`；`4.OLED显示屏介绍.pdf`；`IMG_20261108_224909.jpg`；`IMG_20261108_224916.jpg`。 |
| AMS1117 模块资料 | `AMS1117电源模块/参数图-使用5V版本.png`。 |
| 底盘孔位与装配参考 | `R1四轮两驱差速小车.pdf`；`R1三轮两驱差速小车.pdf`；`R1差速小车板件孔位【孔位图】.pdf`；`R1四驱小车板件孔位【孔位图】.pdf`。 |
| 底盘布局参考图 | `R1四轮两驱差速小车【3D模型截图】.png`。 |
| 主控实际引脚照片 | `IMG_20261108_224833.jpg`；`IMG_20261108_225032.jpg`；`IMG_20261108_225125.jpg`。 |

## 11. 非必需建议

以下项目不混入正式接线表，也不改变当前清单优先原则；只有在后续实物装配确实需要时再单独选型：

- 在 2S 电池总正极增加合适额定值的保险丝，并在结构上防止电池反接或反接时切断逻辑电源。
- 使用带锁扣的端子、压接端子和焊接后热缩管，替代比赛中承受振动的杜邦连接。
- 在电机线和编码器线进入车体控制区前做应力释放，避免拉动端子、线芯疲劳或插头松动。
- 在电池端、TB6612 VM 附近和核心板 5V 入口设置必要的去耦/储能件，位置靠近负载，具体容量与封装按实际线长选型。
- 给车体加可重复测量的轮径、LF04 高度和左右基准标记，减少每次拆装后的重复标定误差。
- 将电源、电机功率线、编码器线和 I2C 线分区走线；必要时使用屏蔽层或绞线，但屏蔽层不能成为第二条地回流路径。
- 如果后续重新制作接口板，给 `PB8/STBY` 增加 10kΩ 接地下拉，使 MCU 复位或高阻期间电机驱动保持待机。当前飞线和面包板阶段依靠软件初始化和断电上电检查。

## 12. 交付检查口径

- 本文件覆盖硬件清单序号 1-44，所有物料都有状态和用途。
- 主设备采用单块 STM32F103C8T6、单块 TB6612FNG、两块 MG310P20、一个 LF04、一个可选 MPU6050、一个可选 OLED 和一个可选 HC-05。
- ST-Link 只在烧录模式使用 3.3V 唯一目标电源；电池运行、Type-C、ST-Link 的目标电源互斥规则已写入正文。
- LF04 原始电平、黑线掩码、语义状态三层定义和 `0000`/`1111` 区分已写入正文。
- API 统一使用 ms、mm、mm/s、mdeg、mdeg/s 和 PWM 权限 -1000..1000。
- 本文件不要求通过修改 `.ioc` 或厂商 HAL 来实现驱动；任何后续外设配置变更仍须独立验证并明确记录。
- 本文件同时记录已完成软件验证与待完成硬件验收，不把尚未执行的场景写成已经通过。
