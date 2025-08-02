#pragma once
#include <Arduino.h>

// ── 用户配置（按你板子实际改） ─────────────────────────────
constexpr uint8_t SI_RST_PIN = 27;  // ← 你的 SI4732 RST 管脚（请按硬件修改）
constexpr uint8_t I2C_SDA_PIN = 21; // ESP32 默认 SDA=21
constexpr uint8_t I2C_SCL_PIN = 22; // ESP32 默认 SCL=22
constexpr uint32_t I2C_FREQ_HZ = 400000UL;
constexpr uint8_t SI_I2C_ADDR = 0x63; // SEN=Vcc 时为 0x63（7-bit）

// 板载参考晶振（外部 32.768 kHz）
constexpr uint32_t SI_XTAL_HZ = 32768UL;

// AM 模式的 IF 频点（10.7 MHz）
constexpr uint32_t SI_IF_kHz = 10700UL;

// ── 对外仅暴露这些简单函数 ─────────────────────────────
bool SI_init_AM_IF107();                         // 上电初始化 + 进入 AM 模式 + 设定 10.7MHz
bool SI_getQuality(int16_t &rssi, int16_t &snr); // 读取当前 RSSI/SNR（轮询）
bool SI_set_mode_AM_IF107();                     // 快速切到 AM@10.7MHz（不重做寻址）
bool SI_set_mode_FM(uint32_t rf_hz);             // 快速切到 FM 并直接调台（单位 Hz）
