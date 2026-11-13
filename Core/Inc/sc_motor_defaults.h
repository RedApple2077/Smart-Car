/**
  ******************************************************************************
  * @file    sc_motor_defaults.h
  * @brief   双轮标定、巡航速度和 PID 的唯一编译期默认入口。
  *
  *          速度闭环参数已在名义标定和双轮悬空条件下完成台架阶跃验收，
  *          TUNING_VALID=1 只覆盖该证据范围。每转计数、轮径、编码器和
  *          电机方向仍需十圈人工核实，因此 CALIBRATION_VALID 保持 0。
  ******************************************************************************
  */

#ifndef __SC_MOTOR_DEFAULTS_H
#define __SC_MOTOR_DEFAULTS_H

#define SC_MOTOR_CALIBRATION_VALID        0u
#define SC_MOTOR_TUNING_VALID             1u

#define SC_MOTOR_LEFT_ENCODER_SIGN        (-1)
#define SC_MOTOR_RIGHT_ENCODER_SIGN       1
#define SC_MOTOR_LEFT_COUNTS_PER_REV      1040u
#define SC_MOTOR_RIGHT_COUNTS_PER_REV     1040u
#define SC_MOTOR_LEFT_WHEEL_DIAMETER_UM   48000
#define SC_MOTOR_RIGHT_WHEEL_DIAMETER_UM  48000
#define SC_MOTOR_LEFT_MOTOR_SIGN          1
#define SC_MOTOR_RIGHT_MOTOR_SIGN         1

/* 名义标定、双轮悬空条件下 200/478 mm/s 各连续三轮 2 s 阶跃通过。 */
#define SC_MOTOR_CRUISE_SPEED_MM_S        478
#define SC_MOTOR_LEFT_FF_MILLI            803
#define SC_MOTOR_LEFT_KP_MILLI            100
#define SC_MOTOR_LEFT_KI_MILLI            750
#define SC_MOTOR_LEFT_KD_MILLI            0
#define SC_MOTOR_RIGHT_FF_MILLI           803
#define SC_MOTOR_RIGHT_KP_MILLI           100
#define SC_MOTOR_RIGHT_KI_MILLI           750
#define SC_MOTOR_RIGHT_KD_MILLI           0

#endif /* __SC_MOTOR_DEFAULTS_H */
