#ifndef ADF4351_CONTROLLER_H
#define ADF4351_CONTROLLER_H

#include <Arduino.h>

/* ═══════════════════════ ADF4351控制器模块 ═══════════════════════ */
class ADF4351_Controller
{
private:
  uint8_t le_pin;
  uint8_t ce_pin;
  uint8_t ld_pin;
  uint8_t sck_pin;
  uint8_t mosi_pin;
  
  uint32_t ref_freq;
  uint32_t if_freq;
  uint16_t r_div;
  uint32_t pfd_freq;
  
  uint32_t reg[6]; // R5…R0（reg[5] == R5）
  uint32_t current_rf_freq; // 当前射频频率
  
  // 写 32 bit 到 ADF4351（MSB first bit‑bang）
  void writeReg(uint32_t data);

public:
  // 构造函数 - 传入引脚配置和频率参数
  ADF4351_Controller(uint8_t le, uint8_t ce, uint8_t ld, uint8_t sck, uint8_t mosi,
                     uint32_t ref_hz = 50000000UL, uint32_t if_hz = 10700000UL, uint16_t r_divider = 1);
  
  // 初始化ADF4351
  void init();
  
  // 设定射频频率 (Hz) - 自动计算本振频率
  bool setRfFrequency(uint32_t rf_freq_hz);
  
  // 直接设定本振频率 (Hz)
  bool setLoFrequency(uint32_t lo_freq_hz);
  
  // 输出使能
  void enable();
  
  // 输出禁用
  void disable();
  
  // 锁定检测
  bool isLocked();
  
  // 获取当前射频频率
  uint32_t getCurrentRfFrequency() const { return current_rf_freq; }
  
  // 获取当前本振频率
  uint32_t getCurrentLoFrequency() const { return (current_rf_freq > if_freq) ? (current_rf_freq - if_freq) : 0; }
  
  // 获取中频
  uint32_t getIfFrequency() const { return if_freq; }
  
  // 打印状态信息（调试用）
  void printStatus();
  
  // 等待锁定，返回锁定状态
  bool waitForLock(uint16_t timeout_ms = 100);
};

#endif // ADF4351_CONTROLLER_H
