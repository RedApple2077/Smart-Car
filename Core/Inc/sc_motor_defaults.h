/**
  ******************************************************************************
  * @file    sc_motor_defaults.h
  * @brief   双轮标定、巡航速度和 PID 的唯一编译期默认入口。
  *
  *          48.60 mm 游标卡尺标尺下，200/500 mm/s 各连续三轮闭环阶跃
  *          已通过，TUNING_VALID=1 只覆盖该台架证据。每转计数、落地有效
  *          滚动周长和电机方向仍需十圈/里程人工核实，CALIBRATION_VALID
  *          保持 0。
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
#define SC_MOTOR_LEFT_WHEEL_DIAMETER_UM   48600
#define SC_MOTOR_RIGHT_WHEEL_DIAMETER_UM  48600
#define SC_MOTOR_LEFT_MOTOR_SIGN          1
#define SC_MOTOR_RIGHT_MOTOR_SIGN         1

/* 48.60 mm Identify 与 200/500 mm/s 双闭环三轮台架验收通过。 */
#define SC_MOTOR_CRUISE_SPEED_MM_S        500
#define SC_MOTOR_LEFT_FF_MILLI            798
#define SC_MOTOR_LEFT_KP_MILLI            100
#define SC_MOTOR_LEFT_KI_MILLI            750
#define SC_MOTOR_LEFT_KD_MILLI            0
#define SC_MOTOR_RIGHT_FF_MILLI           789
#define SC_MOTOR_RIGHT_KP_MILLI           100
#define SC_MOTOR_RIGHT_KI_MILLI           750
#define SC_MOTOR_RIGHT_KD_MILLI           0

#endif /* __SC_MOTOR_DEFAULTS_H */
