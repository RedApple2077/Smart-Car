/**
  ******************************************************************************
  * @file    sc_motor_defaults.h
  * @brief   双轮标定、巡航速度和 PID 的唯一编译期默认入口。
  *
  *          当前标定和调参均未通过实车验证。后续 Bench 实测通过后只修改
  *          本文件中的常量，并把对应的 VALID 标志改为 1；不得把占位值
  *          宣称为已验证参数。
  ******************************************************************************
  */

#ifndef __SC_MOTOR_DEFAULTS_H
#define __SC_MOTOR_DEFAULTS_H

#define SC_MOTOR_CALIBRATION_VALID        0u
#define SC_MOTOR_TUNING_VALID             0u

#define SC_MOTOR_LEFT_ENCODER_SIGN        (-1)
#define SC_MOTOR_RIGHT_ENCODER_SIGN       1
#define SC_MOTOR_LEFT_COUNTS_PER_REV      1040u
#define SC_MOTOR_RIGHT_COUNTS_PER_REV     1040u
#define SC_MOTOR_LEFT_WHEEL_DIAMETER_UM   48000
#define SC_MOTOR_RIGHT_WHEEL_DIAMETER_UM  48000
#define SC_MOTOR_LEFT_MOTOR_SIGN          1
#define SC_MOTOR_RIGHT_MOTOR_SIGN         1

/* 以下数值是显式占位默认，仅用于后续受控实验的起点。 */
#define SC_MOTOR_CRUISE_SPEED_MM_S        200
#define SC_MOTOR_LEFT_FF_MILLI            1000
#define SC_MOTOR_LEFT_KP_MILLI            100
#define SC_MOTOR_LEFT_KI_MILLI            10
#define SC_MOTOR_LEFT_KD_MILLI            0
#define SC_MOTOR_RIGHT_FF_MILLI           1000
#define SC_MOTOR_RIGHT_KP_MILLI           100
#define SC_MOTOR_RIGHT_KI_MILLI           10
#define SC_MOTOR_RIGHT_KD_MILLI           0

#endif /* __SC_MOTOR_DEFAULTS_H */
