/**
  ******************************************************************************
  * @file    sc_motor_defaults.h
  * @brief   双轮标定、巡航速度和 PID 的唯一编译期默认入口。
  *
  *          左右 CPR 均为 1061；1061 CPR 下 200/500 mm/s 各连续三轮
  *          闭环验收通过，TUNING_VALID=1 只覆盖该台架范围。标定依据
  *          48.600 mm 卡尺、十圈 CPR 和方向/正命令前进验证；落地滚动
  *          周长未测但经用户明确豁免，CALIBRATION_VALID=1。
  ******************************************************************************
  */

#ifndef __SC_MOTOR_DEFAULTS_H
#define __SC_MOTOR_DEFAULTS_H

#define SC_MOTOR_CALIBRATION_VALID        1u
#define SC_MOTOR_TUNING_VALID             1u

#define SC_MOTOR_LEFT_ENCODER_SIGN        (-1)
#define SC_MOTOR_RIGHT_ENCODER_SIGN       1
#define SC_MOTOR_LEFT_COUNTS_PER_REV      1061u
#define SC_MOTOR_RIGHT_COUNTS_PER_REV     1061u
#define SC_MOTOR_LEFT_WHEEL_DIAMETER_UM   48600
#define SC_MOTOR_RIGHT_WHEEL_DIAMETER_UM  48600
#define SC_MOTOR_LEFT_MOTOR_SIGN          1
#define SC_MOTOR_RIGHT_MOTOR_SIGN         1

/* 1061 CPR Identify + 200/500 mm/s 双闭环六轮台架验收通过。 */
#define SC_MOTOR_CRUISE_SPEED_MM_S        500
#define SC_MOTOR_LEFT_FF_MILLI            810
#define SC_MOTOR_LEFT_KP_MILLI            100
#define SC_MOTOR_LEFT_KI_MILLI            750
#define SC_MOTOR_LEFT_KD_MILLI            0
#define SC_MOTOR_RIGHT_FF_MILLI           815
#define SC_MOTOR_RIGHT_KP_MILLI           100
#define SC_MOTOR_RIGHT_KI_MILLI           750
#define SC_MOTOR_RIGHT_KD_MILLI           0

#endif /* __SC_MOTOR_DEFAULTS_H */
