#pragma once
#include <Arduino.h>

/* ───────────────────────── ① 用户配置区（按需改） ───────────────────────── */
// 参考时钟 & 中频
constexpr uint32_t REF_Hz = 50000000UL; // 板载 50 MHz 晶振
constexpr uint32_t IF_Hz = 10700000UL;  // 中频 10.7 MHz（SA602）

// ESP32 ↔ ADF4351 引脚映射
constexpr uint8_t ADF_LE = 5;    // SPI-CS / LE
constexpr uint8_t ADF_SCK = 18;  // SPI-CLK
constexpr uint8_t ADF_MOSI = 23; // SPI-MOSI
constexpr uint8_t ADF_CE = 32;   // PLL_CE
constexpr uint8_t ADF_LD = 33;   // PLL_LD（数字锁定指示，高=锁定）

// ADF4351 R 分频器（寄存器当前写死为 1；如需修改，请同时更新 .cpp 中 R2_BASE）
constexpr uint16_t R_DIV = 1;

/* ───────────────────────── ② 单函数接口 ─────────────────────────
 * 设置 ADF4351 输出：根据传入射频 RF（Hz），按低变频方式计算 LO=RF-IF 并配置芯片。
 * 成功返回：当前 LO 频率（Hz）
 * 失败返回：0
 * 本函数会在串口打印一行 "[ADF4351] RF=xxxMHz -> LO=xxxMHz : LOCK/UNLOCK"
 */
uint32_t ADF4351_setRF(uint32_t rf_hz);
