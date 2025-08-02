#include <Arduino.h>
#include "ADF4351_Controller.h"

/* ───────────────────────── ① 用户配置区 ───────────────────────── */
// --- 参考时钟 & 中频 ---
constexpr uint32_t REF_Hz = 50000000UL; // 板载 50 MHz 晶振
constexpr uint32_t IF_Hz = 10700000UL;  // 中频 10.7 MHz（SA602）

// --- ESP32 ↔ ADF4351 引脚映射 ---
constexpr uint8_t ADF_LE = 5;    // SPI‑CS / LE
constexpr uint8_t ADF_SCK = 18;  // SPI‑CLK
constexpr uint8_t ADF_MOSI = 23; // SPI‑MOSI
constexpr uint8_t ADF_CE = 32;   // PLL_CE
constexpr uint8_t ADF_LD = 33;   // PLL_LD

// --- ADF4351 PLL 参数 ---
constexpr uint16_t R_DIV = 1; // R 分频器

// --- 可变射频频率 - 这个值可以在运行时动态修改 ---
uint32_t rfHz = 108000000UL; // 当前射频频率，默认108MHz (FM)

// --- 其他应用模块变量 ---
bool frequency_changed = false; // 频率改变标志
unsigned long last_update = 0; // 上次更新时间
// ───────────────────────────────────────────────────────────────── */

/* ========================= 模块实例化 ========================= */
ADF4351_Controller adf_controller(ADF_LE, ADF_CE, ADF_LD, ADF_SCK, ADF_MOSI, REF_Hz, IF_Hz, R_DIV);

/* ========================= 频率控制函数 ========================= */
// 设置射频频率（可从其他模块调用）
bool setRfFrequency(uint32_t new_rf_hz)
{
  if (new_rf_hz != rfHz)
  {
    rfHz = new_rf_hz;
    frequency_changed = true;
    Serial.printf("📻 射频频率更改为: %.3f MHz\n", rfHz / 1e6);
    return true;
  }
  return false;
}

// 获取当前射频频率
uint32_t getRfFrequency()
{
  return rfHz;
}

// 频率扫描示例函数
void frequencyScan(uint32_t start_hz, uint32_t end_hz, uint32_t step_hz)
{
  Serial.printf("🔍 开始频率扫描: %.1f - %.1f MHz, 步进 %.1f kHz\n", 
                start_hz / 1e6, end_hz / 1e6, step_hz / 1e3);
  
  for (uint32_t freq = start_hz; freq <= end_hz; freq += step_hz)
  {
    setRfFrequency(freq);
    
    if (adf_controller.setRfFrequency(rfHz))
    {
      if (adf_controller.waitForLock(50))
      {
        Serial.printf("✓ %.3f MHz - 锁定\n", freq / 1e6);
      }
      else
      {
        Serial.printf("✗ %.3f MHz - 未锁定\n", freq / 1e6);
      }
    }
    delay(100); // 扫描间隔
  }
  Serial.println("🔍 频率扫描完成");
}

/* ========================= 主程序初始化 ========================= */
void setup()
{
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=======================================");
  Serial.println("    SI4732 + ADF4351 控制系统启动");
  Serial.println("=======================================");
  
  // 初始化ADF4351控制器
  adf_controller.init();
  adf_controller.enable();
  
  // 设置初始频率
  Serial.printf("🎯 设置初始射频频率: %.3f MHz\n", rfHz / 1e6);
  if (adf_controller.setRfFrequency(rfHz))
  {
    adf_controller.waitForLock(100);
    adf_controller.printStatus();
  }
  
  Serial.println("\n✅ 系统初始化完成");
  Serial.println("📝 在loop()中可以动态修改 rfHz 变量来改变频率");
  Serial.println("📝 或调用 setRfFrequency() 函数");
  
  last_update = millis();
}

/* ========================= 主循环 ========================= */
void loop()
{
  unsigned long current_time = millis();
  
  // 检查频率是否需要更新（每秒检查一次或频率改变时）
  if (frequency_changed || (current_time - last_update >= 1000))
  {
    if (frequency_changed)
    {
      Serial.printf("🔄 应用频率更改: %.3f MHz\n", rfHz / 1e6);
      
      if (adf_controller.setRfFrequency(rfHz))
      {
        if (adf_controller.waitForLock(100))
        {
          Serial.printf("✅ 频率设置成功: RF=%.3f MHz, LO=%.3f MHz\n", 
                        rfHz / 1e6, adf_controller.getCurrentLoFrequency() / 1e6);
        }
        else
        {
          Serial.printf("⚠️  PLL未锁定，请检查频率设置\n");
        }
      }
      frequency_changed = false;
    }
    
    last_update = current_time;
  }
  
  // ═══════════════════════════════════════════════════════════════
  // 🔥 在这里添加你的其他应用代码（SI4732控制等）
  // ═══════════════════════════════════════════════════════════════
  
  // 示例：简单的频率变化演示（每5秒切换频率）
  static unsigned long demo_timer = 0;
  static uint8_t demo_freq_index = 0;
  static uint32_t demo_frequencies[] = {
    108000000UL,  // 108.0 MHz FM
    101500000UL,  // 101.5 MHz FM  
    95300000UL,   // 95.3 MHz FM
    88100000UL    // 88.1 MHz FM
  };
  
  if (current_time - demo_timer >= 5000) // 每5秒切换
  {
    demo_freq_index = (demo_freq_index + 1) % 4;
    setRfFrequency(demo_frequencies[demo_freq_index]);
    demo_timer = current_time;
  }
  
  // 检查串口命令（可选）
  if (Serial.available())
  {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.startsWith("freq "))
    {
      float freq_mhz = command.substring(5).toFloat();
      if (freq_mhz > 0)
      {
        uint32_t freq_hz = (uint32_t)(freq_mhz * 1e6);
        setRfFrequency(freq_hz);
      }
    }
    else if (command == "status")
    {
      adf_controller.printStatus();
    }
    else if (command == "scan")
    {
      // FM波段扫描示例：88-108 MHz
      frequencyScan(88000000UL, 108000000UL, 200000UL); // 200kHz步进
    }
    else if (command == "help")
    {
      Serial.println("\n📖 可用命令:");
      Serial.println("  freq <MHz>  - 设置射频频率，例如: freq 101.5");
      Serial.println("  status      - 显示当前状态");
      Serial.println("  scan        - FM波段扫描 (88-108 MHz)");
      Serial.println("  help        - 显示此帮助");
    }
  }
  
  delay(10); // 主循环延时
}
