# Smart-Car

基于 STM32F103C8T6 的四轮两驱循迹小车工程。目标主链路为：

```text
STM32F103C8T6 -> TB6612FNG -> MG310P20 左右电机
```

LF04 四路红外模块提供循迹输入；MPU6050、SSD1306 OLED 和 HC-05 作为可选辅助设备。完整硬件清单、接线、软件分层与 API 规划见[硬件接线与软件 API 规划](docs/循迹小车硬件接线与软件API规划.md)。

## 硬件基线

- 主控：STM32F103C8T6 核心板。
- 驱动与执行器：单块 TB6612FNG、两台带霍尔编码器的 MG310P20 直流减速电机。
- 底盘：后端两轮驱动，前端两个万向轮。
- 循迹输入：LF04 四路红外巡线模块。
- 辅助设备：MPU6050、SSD1306 OLED、HC-05；CH340G 用于台架串口调试。

## 当前状态

截至 2026-11-09，工程处于 CubeMX/HAL 外设初始化阶段：

- 已生成并初始化 72 MHz HSE/PLL 时钟、I2C2、TIM2/TIM3 编码器接口、TIM4 双路 PWM、USART1/USART2，以及电机方向、STBY、LF04、启动按键和状态 LED 的 GPIO。
- `Core/Src/main.c` 的 `main()` 已完成 HAL 与外设初始化，但主循环为空，定时器计数/PWM 尚未启动。
- 尚未实现循迹策略、双轮速度闭环、应用安全门、设备驱动或规划文档第 7 节所列业务 API。
- 尚未烧录、联调硬件或进行实车验证。当前只能确认工程能够构建，不能据此确认比赛功能可用。

## 构建

需要 CMake 3.22 或更高版本、Ninja 和 Arm GNU Toolchain（`arm-none-eabi-gcc`）。

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

构建产物为 `build/Debug/Smart-Car.elf`。当前已验证 `Debug` 预设的配置、编译和链接；`Release` 预设已定义但本次未验证。
