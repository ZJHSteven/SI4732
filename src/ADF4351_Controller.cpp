#include "ADF4351_Controller.h"

/* ========================= ADF4351控制器实现 ========================= */

ADF4351_Controller::ADF4351_Controller(uint8_t le, uint8_t ce, uint8_t ld, uint8_t sck, uint8_t mosi,
                                       uint32_t ref_hz, uint32_t if_hz, uint16_t r_divider)
    : le_pin(le), ce_pin(ce), ld_pin(ld), sck_pin(sck), mosi_pin(mosi),
      ref_freq(ref_hz), if_freq(if_hz), r_div(r_divider), current_rf_freq(0)
{
  pfd_freq = ref_freq / r_div; // 鉴相频率
  
  /* 默认寄存器值 ‑ 依据数据手册 + 常用配置
     ‑ Digital Lock Detect
     ‑ 输出功率 +5 dBm / RF_OUT_EN=1 / MTLD=0
     ‑ 充电泵极性负向（环路常用，若板子做成正向请把 reg[2] 改 0x0E008E62）
  */
  reg[5] = 0x00580005; // R5
  reg[4] = 0x008C703C; // R4  (+5 dBm，RF_OUT_EN=1，MTLD=0)
  reg[3] = 0x00C804B3; // R3  BS_CLK_DIV = 400 (50 MHz / 125 kHz)
  reg[2] = 0x0E008E42; // R2  PD_POL=0, CP=2.5 mA
  reg[1] = 0x80008001; // R1  Phase=1, MOD 占位
  reg[0] = 0x00800000; // R0  INT/FRAC 占位
}

void ADF4351_Controller::writeReg(uint32_t data)
{
  digitalWrite(le_pin, LOW);
  delayMicroseconds(1);
  for (int i = 31; i >= 0; --i)
  {
    digitalWrite(sck_pin, LOW);
    digitalWrite(mosi_pin, (data >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(sck_pin, HIGH);
    delayMicroseconds(1);
  }
  digitalWrite(le_pin, HIGH);
  delayMicroseconds(1);
  digitalWrite(le_pin, LOW);
}

void ADF4351_Controller::init()
{
  pinMode(le_pin, OUTPUT);
  pinMode(ce_pin, OUTPUT);
  pinMode(ld_pin, INPUT_PULLUP);
  pinMode(sck_pin, OUTPUT);
  pinMode(mosi_pin, OUTPUT);
  
  digitalWrite(le_pin, LOW);
  digitalWrite(ce_pin, HIGH); // 使能芯片
  delay(10);
  
  // R5→R0 顺序写入
  for (int i = 5; i >= 0; --i)
  {
    writeReg(reg[i]);
    delayMicroseconds(200);
  }
  delay(10);
  
  Serial.println("ADF4351 控制器初始化完成");
  Serial.printf("参考频率: %.1f MHz\n", ref_freq / 1e6);
  Serial.printf("中频: %.1f MHz\n", if_freq / 1e6);
  Serial.printf("鉴相频率: %.1f MHz\n", pfd_freq / 1e6);
}

bool ADF4351_Controller::setRfFrequency(uint32_t rf_freq_hz)
{
  current_rf_freq = rf_freq_hz;
  
  // 计算本振频率：LO = RF - IF (低变频)
  uint32_t lo_freq = (rf_freq_hz > if_freq) ? (rf_freq_hz - if_freq) : 35000000UL;
  
  if (lo_freq < 35000000UL)
  {
    lo_freq = 35000000UL;
    Serial.printf("警告: 本振频率过低，设置为最小值 35 MHz\n");
  }
  
  return setLoFrequency(lo_freq);
}

bool ADF4351_Controller::setLoFrequency(uint32_t lo_freq_hz)
{
  if (lo_freq_hz < 35000000UL || lo_freq_hz > 4400000000UL)
  {
    Serial.printf("错误: 本振频率 %.3f MHz 超出 35 MHz–4.4 GHz 范围\n", lo_freq_hz / 1e6);
    return false;
  }

  /* 计算 VCO 频率 & RF_DIV_SEL */
  uint8_t rf_div_sel = 0;
  uint32_t vco_freq = lo_freq_hz;
  while (vco_freq < 2200000000UL && rf_div_sel < 6)
  {
    vco_freq <<= 1; // ×2
    ++rf_div_sel;
  }
  if (vco_freq > 4400000000UL)
  {
    Serial.println("错误: VCO 频率超限");
    return false;
  }

  /* 计算 INT / FRAC / MOD */
  const uint16_t MOD = 4095;
  uint16_t INT = vco_freq / pfd_freq;
  uint32_t remainder = vco_freq % pfd_freq;
  uint16_t FRAC = (remainder == 0) ? 0 : (uint16_t)(((uint64_t)remainder * MOD) / pfd_freq); // 64 bit 乘法防溢出

  if (INT < 23 || INT > 65535)
  {
    Serial.println("错误: INT 超范围");
    return false;
  }

  /* 构建寄存器 */
  reg[0] = ((uint32_t)INT << 15) | ((uint32_t)FRAC << 3);
  reg[1] = (1UL << 15) | ((uint32_t)MOD << 3) | 0x1;
  reg[4] = (reg[4] & 0xFF8FFFFF) | ((uint32_t)rf_div_sel << 20);

  /* 写入 R5→R0 */
  writeReg(reg[5]);
  writeReg(reg[4]);
  writeReg(reg[3]);
  writeReg(reg[2]);
  writeReg(reg[1]);
  writeReg(reg[0]);
  delayMicroseconds(300);

  if (!silent_mode) {
    Serial.printf("PLL参数: VCO=%.3f MHz, INT=%u, FRAC=%u, MOD=%u, RF_DIV=%u\n",
                  vco_freq / 1e6, INT, FRAC, MOD, 1u << rf_div_sel);
    Serial.printf("设置: RF=%.3f MHz → LO=%.3f MHz\n", 
                  current_rf_freq / 1e6, lo_freq_hz / 1e6);
  }
  return true;
}

void ADF4351_Controller::enable()
{
  writeReg(reg[4]); // R4 里已包含 RF_OUT_EN=1 / +5 dBm / MTLD=0
}

void ADF4351_Controller::disable()
{
  uint32_t temp_r4 = reg[4] & 0xFFFFFFDF; // 清除 RF_OUT_EN 位 (bit 5)
  writeReg(temp_r4);
}

bool ADF4351_Controller::isLocked()
{
  return digitalRead(ld_pin); // Digital LD: 高电平 = 锁定
}

bool ADF4351_Controller::waitForLock(uint16_t timeout_ms)
{
  for (uint16_t i = 0; i < timeout_ms; ++i)
  {
    if (isLocked())
    {
      if (!silent_mode) {
        Serial.printf("✓ PLL锁定成功 (%d ms)\n", i + 1);
      }
      return true;
    }
    delay(1);
  }
  if (!silent_mode) {
    Serial.printf("✗ PLL锁定超时 (%d ms)\n", timeout_ms);
  }
  return false;
}

void ADF4351_Controller::setSilentMode(bool silent)
{
  silent_mode = silent;
}

void ADF4351_Controller::printStatus()
{
  Serial.printf("\n=== ADF4351 状态信息 ===\n");
  Serial.printf("射频频率: %.3f MHz\n", current_rf_freq / 1e6);
  Serial.printf("本振频率: %.3f MHz\n", getCurrentLoFrequency() / 1e6);
  Serial.printf("中频: %.3f MHz\n", if_freq / 1e6);
  Serial.printf("参考频率: %.3f MHz\n", ref_freq / 1e6);
  Serial.printf("鉴相频率: %.3f MHz\n", pfd_freq / 1e6);
  Serial.printf("锁定状态: %s (LD=%d)\n", isLocked() ? "已锁定" : "未锁定", digitalRead(ld_pin));
  
  uint16_t INT = (reg[0] >> 15) & 0xFFFF;
  uint16_t FRAC = (reg[0] >> 3) & 0xFFF;
  uint16_t MOD = (reg[1] >> 3) & 0xFFF;
  uint8_t rf_d = (reg[4] >> 20) & 0x7;
  Serial.printf("PLL参数: INT=%u, FRAC=%u, MOD=%u, RF_DIV=2^%u\n", INT, FRAC, MOD, rf_d);
  
  uint64_t vco = (uint64_t)INT * pfd_freq + ((uint64_t)FRAC * pfd_freq) / MOD;
  Serial.printf("VCO=%.6f MHz, 输出=%.6f MHz\n", vco / 1e6, (double)vco / (1UL << rf_d) / 1e6);
  
  Serial.println("寄存器值:");
  for (int i = 5; i >= 0; --i)
    Serial.printf("  R%d: 0x%08X\n", i, reg[i]);
  Serial.println("========================\n");
}
