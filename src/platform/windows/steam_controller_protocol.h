#pragma once

#include <stdint.h>

typedef uint8_t sc_u8;
typedef uint16_t sc_u16;
typedef uint32_t sc_u32;
typedef int16_t sc_i16;

#define SC_VALVE_USB_VID 0x28DE
#define SC_PID_TRITON_BLE 0x1303

#define SC_FEATURE_REPORT_ID 0x01
#define SC_FEATURE_REPORT_BYTES 64

#define SC_REPORT_ID_TRITON_CONTROLLER_STATE 0x42
#define SC_REPORT_ID_TRITON_BATTERY_STATUS 0x43
#define SC_REPORT_ID_TRITON_CONTROLLER_STATE_BLE 0x45
#define SC_REPORT_ID_TRITON_WIRELESS_STATUS_X 0x46
#define SC_REPORT_ID_TRITON_WIRELESS_STATUS 0x79

#define SC_OUT_REPORT_HAPTIC_RUMBLE 0x80
#define SC_OUT_REPORT_HAPTIC_PULSE 0x81
#define SC_OUT_REPORT_HAPTIC_COMMAND 0x82
#define SC_OUT_REPORT_HAPTIC_LFO_TONE 0x83
#define SC_OUT_REPORT_HAPTIC_LOG_SWEEP 0x85
#define SC_OUT_REPORT_HAPTIC_SCRIPT 0x86

#define SC_TRITON_SENSOR_UPDATE_INTERVAL_US 4032

#define SC_TRITON_WIRELESS_DISCONNECT 1
#define SC_TRITON_WIRELESS_CONNECT 2

#define SC_TRITON_BUTTON_A 0x00000001u
#define SC_TRITON_BUTTON_B 0x00000002u
#define SC_TRITON_BUTTON_X 0x00000004u
#define SC_TRITON_BUTTON_Y 0x00000008u
#define SC_TRITON_BUTTON_QAM 0x00000010u
#define SC_TRITON_BUTTON_R3 0x00000020u
#define SC_TRITON_BUTTON_VIEW 0x00000040u
#define SC_TRITON_BUTTON_R4 0x00000080u
#define SC_TRITON_BUTTON_R5 0x00000100u
#define SC_TRITON_BUTTON_R 0x00000200u
#define SC_TRITON_BUTTON_DPAD_DOWN 0x00000400u
#define SC_TRITON_BUTTON_DPAD_RIGHT 0x00000800u
#define SC_TRITON_BUTTON_DPAD_LEFT 0x00001000u
#define SC_TRITON_BUTTON_DPAD_UP 0x00002000u
#define SC_TRITON_BUTTON_MENU 0x00004000u
#define SC_TRITON_BUTTON_L3 0x00008000u
#define SC_TRITON_BUTTON_STEAM 0x00010000u
#define SC_TRITON_BUTTON_L4 0x00020000u
#define SC_TRITON_BUTTON_L5 0x00040000u
#define SC_TRITON_BUTTON_L 0x00080000u
#define SC_TRITON_TOUCH_RIGHT_STICK 0x00100000u
#define SC_TRITON_TOUCH_RIGHT_PAD 0x00200000u
#define SC_TRITON_CLICK_RIGHT_PAD 0x00400000u
#define SC_TRITON_CLICK_RIGHT_TRIGGER 0x00800000u
#define SC_TRITON_TOUCH_LEFT_STICK 0x01000000u
#define SC_TRITON_TOUCH_LEFT_PAD 0x02000000u
#define SC_TRITON_CLICK_LEFT_PAD 0x04000000u
#define SC_TRITON_CLICK_LEFT_TRIGGER 0x08000000u
#define SC_TRITON_TOUCH_RIGHT_AUX 0x10000000u
#define SC_TRITON_TOUCH_LEFT_AUX 0x20000000u

#pragma pack(push, 1)

typedef struct sc_triton_imu_no_quat_t {
  sc_u32 timestamp;
  sc_i16 accel_x;
  sc_i16 accel_y;
  sc_i16 accel_z;
  sc_i16 gyro_x;
  sc_i16 gyro_y;
  sc_i16 gyro_z;
} sc_triton_imu_no_quat_t;

typedef struct sc_triton_state_no_quat_t {
  sc_u8 seq_num;
  sc_u32 buttons;
  sc_i16 trigger_left;
  sc_i16 trigger_right;
  sc_i16 left_stick_x;
  sc_i16 left_stick_y;
  sc_i16 right_stick_x;
  sc_i16 right_stick_y;
  sc_i16 left_pad_x;
  sc_i16 left_pad_y;
  sc_u16 pressure_left;
  sc_i16 right_pad_x;
  sc_i16 right_pad_y;
  sc_u16 pressure_right;
  sc_triton_imu_no_quat_t imu;
} sc_triton_state_no_quat_t;

typedef struct sc_triton_battery_status_t {
  sc_u8 charge_state;
  sc_u8 battery_level;
  sc_u16 battery_voltage;
  sc_u16 system_voltage;
  sc_u16 input_voltage;
  sc_u16 current;
  sc_u16 input_current;
  sc_u16 temperature;
} sc_triton_battery_status_t;

typedef struct sc_triton_wireless_status_t {
  sc_u8 state;
} sc_triton_wireless_status_t;

#pragma pack(pop)

#define SC_TRITON_STATE_NO_QUAT_BYTES ((sc_u16) sizeof(sc_triton_state_no_quat_t))
#define SC_TRITON_BATTERY_STATUS_BYTES ((sc_u16) sizeof(sc_triton_battery_status_t))
#define SC_TRITON_WIRELESS_STATUS_BYTES ((sc_u16) sizeof(sc_triton_wireless_status_t))
