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
    // 仅在非扫频模式下输出详细信息
    if (!adf_controller.getSilentMode()) {
      Serial.printf("📻 射频频率更改为: %.3f MHz\n", rfHz / 1e6);
    }
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
  Serial.println("📝 快速扫频模式已启动：88MHz → 108MHz");
  Serial.println("📝 输入 'help' 查看所有可用命令");
  Serial.printf("📊 默认扫频参数：步进100kHz，间隔15ms，单程约%.1f秒\n", 
                (float)((108000000UL - 88000000UL) / 100000UL + 1) * 15 / 1000.0);
  
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
      if (!adf_controller.getSilentMode()) {
        Serial.printf("🔄 应用频率更改: %.3f MHz\n", rfHz / 1e6);
      }
      
      if (adf_controller.setRfFrequency(rfHz))
      {
        if (adf_controller.waitForLock(100))
        {
          if (!adf_controller.getSilentMode()) {
            Serial.printf("✅ 频率设置成功: RF=%.3f MHz, LO=%.3f MHz\n", 
                          rfHz / 1e6, adf_controller.getCurrentLoFrequency() / 1e6);
          }
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
  // 🔥 快速扫频功能：88MHz → 108MHz，3-5秒内完成
  // ═══════════════════════════════════════════════════════════════
  
  static bool fast_scan_active = true;        // 是否启用快速扫频
  static unsigned long scan_timer = 0;        // 扫频计时器
  static unsigned long scan_start_time = 0;   // 扫频开始时间
  static uint32_t scan_start_freq = 88000000UL;  // 起始频率 88MHz
  static uint32_t scan_end_freq = 108000000UL;   // 结束频率 108MHz
  static uint32_t scan_step = 100000UL;          // 步进 100kHz (可调整)
  static uint32_t current_scan_freq = 88000000UL; // 当前扫频频率
  static unsigned long scan_interval = 1;       // 扫频间隔(ms) - 可调整
  static bool scan_direction = true;             // true=向上扫，false=向下扫
  static uint32_t scan_count = 0;               // 扫频计数器
  
  // 计算扫频参数
  static uint32_t total_steps = (scan_end_freq - scan_start_freq) / scan_step + 1;
  static uint32_t total_scan_time = total_steps * scan_interval; // 总扫频时间
  
  if (fast_scan_active && (current_time - scan_timer >= scan_interval))
  {
    // 启动扫频时开启静默模式
    if (scan_count == 0) {
      adf_controller.setSilentMode(true);
      scan_start_time = current_time;
      Serial.printf("🚀 开始快速扫频: %.1f → %.1f MHz (步进%.0f kHz)\n", 
                    scan_start_freq / 1e6, scan_end_freq / 1e6, scan_step / 1e3);
    }
    
    // 执行扫频步进
    setRfFrequency(current_scan_freq);
    scan_count++;
    
    // 计算下一个频率
    if (scan_direction) // 向上扫频
    {
      current_scan_freq += scan_step;
      if (current_scan_freq >= scan_end_freq)
      {
        current_scan_freq = scan_end_freq;
        scan_direction = false; // 切换为向下扫频
        
        // 完成一次单向扫频，显示结果
        unsigned long elapsed = current_time - scan_start_time;
        Serial.printf("⬆️  向上扫频完成: %.1f秒，%u步\n", elapsed / 1000.0, scan_count);
        scan_count = 0;
        scan_start_time = current_time;
      }
    }
    else // 向下扫频
    {
      current_scan_freq -= scan_step;
      if (current_scan_freq <= scan_start_freq)
      {
        current_scan_freq = scan_start_freq;
        scan_direction = true; // 切换为向上扫频
        
        // 完成一次单向扫频，显示结果
        unsigned long elapsed = current_time - scan_start_time;
        Serial.printf("⬇️  向下扫频完成: %.1f秒，%u步\n", elapsed / 1000.0, scan_count);
        scan_count = 0;
        scan_start_time = current_time;
      }
    }
    
    scan_timer = current_time;
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
        fast_scan_active = false; // 停止快速扫频
        adf_controller.setSilentMode(false); // 关闭静默模式
        setRfFrequency(freq_hz);
        Serial.printf("📻 手动设置频率，快速扫频已停止\n");
      }
    }
    else if (command == "status")
    {
      adf_controller.printStatus();
    }
    else if (command == "scan")
    {
      // FM波段扫描示例：88-108 MHz
      fast_scan_active = false; // 停止快速扫频
      adf_controller.setSilentMode(false); // 关闭静默模式
      frequencyScan(88000000UL, 108000000UL, 200000UL); // 200kHz步进
    }
    else if (command == "fastscan")
    {
      fast_scan_active = !fast_scan_active;
      Serial.printf("🔄 快速扫频 %s\n", fast_scan_active ? "已启用" : "已停止");
      if (fast_scan_active)
      {
        current_scan_freq = scan_start_freq;
        scan_direction = true;
        scan_timer = millis();
        scan_count = 0;
        adf_controller.setSilentMode(true); // 开启静默模式
      }
      else
      {
        adf_controller.setSilentMode(false); // 关闭静默模式
      }
    }
    else if (command.startsWith("step "))
    {
      float step_khz = command.substring(5).toFloat();
      if (step_khz > 0)
      {
        scan_step = (uint32_t)(step_khz * 1000);
        total_steps = (scan_end_freq - scan_start_freq) / scan_step + 1;
        total_scan_time = total_steps * scan_interval;
        Serial.printf("📐 扫频步进设置为 %.0f kHz，单程需要 %.1f 秒\n", 
                      scan_step / 1e3, total_scan_time / 1000.0);
      }
    }
    else if (command.startsWith("interval "))
    {
      uint16_t interval_ms = command.substring(9).toInt();
      if (interval_ms >= 5 && interval_ms <= 1000)
      {
        scan_interval = interval_ms;
        total_scan_time = total_steps * scan_interval;
        Serial.printf("⏱️  扫频间隔设置为 %u ms，单程需要 %.1f 秒\n", 
                      scan_interval, total_scan_time / 1000.0);
      }
    }
    else if (command.startsWith("range "))
    {
      // 格式: range 88.0 108.0
      int space_pos = command.indexOf(' ', 6);
      if (space_pos > 0)
      {
        float start_mhz = command.substring(6, space_pos).toFloat();
        float end_mhz = command.substring(space_pos + 1).toFloat();
        if (start_mhz > 0 && end_mhz > start_mhz)
        {
          scan_start_freq = (uint32_t)(start_mhz * 1e6);
          scan_end_freq = (uint32_t)(end_mhz * 1e6);
          current_scan_freq = scan_start_freq;
          total_steps = (scan_end_freq - scan_start_freq) / scan_step + 1;
          total_scan_time = total_steps * scan_interval;
          Serial.printf("📡 扫频范围设置为 %.1f - %.1f MHz，单程需要 %.1f 秒\n", 
                        scan_start_freq / 1e6, scan_end_freq / 1e6, total_scan_time / 1000.0);
        }
      }
    }
    else if (command == "help")
    {
      Serial.println("\n📖 可用命令:");
      Serial.println("  freq <MHz>      - 设置射频频率，例如: freq 101.5");
      Serial.println("  status          - 显示当前状态");
      Serial.println("  scan            - FM波段扫描 (88-108 MHz)");
      Serial.println("  fastscan        - 开启/关闭快速扫频");
      Serial.println("  step <kHz>      - 设置扫频步进，例如: step 100");
      Serial.println("  interval <ms>   - 设置扫频间隔，例如: interval 15");
      Serial.println("  range <MHz MHz> - 设置扫频范围，例如: range 88.0 108.0");
      Serial.println("  speedtest       - 测试频率切换速度");
      Serial.println("  help            - 显示此帮助");
      Serial.printf("\n当前扫频设置:\n");
      Serial.printf("  范围: %.1f - %.1f MHz\n", scan_start_freq / 1e6, scan_end_freq / 1e6);
      Serial.printf("  步进: %.0f kHz\n", scan_step / 1e3);
      Serial.printf("  间隔: %lu ms\n", scan_interval);
      Serial.printf("  单程时间: %.1f 秒\n", total_scan_time / 1000.0);
      Serial.printf("  快速扫频: %s\n", fast_scan_active ? "启用" : "停止");
    }
    else if (command == "speedtest")
    {
      // 速度测试：测试10次频率切换的平均时间
      fast_scan_active = false;
      adf_controller.setSilentMode(false);
      Serial.println("🔬 开始频率切换速度测试...");
      
      uint32_t test_freqs[] = {88000000UL, 95000000UL, 102000000UL, 108000000UL};
      unsigned long start_time = millis();
      
      for (int i = 0; i < 10; i++)
      {
        uint32_t freq = test_freqs[i % 4];
        unsigned long freq_start = micros();
        
        setRfFrequency(freq);
        adf_controller.setRfFrequency(rfHz);
        bool locked = adf_controller.waitForLock(50);
        
        unsigned long freq_end = micros();
        Serial.printf("  第%d次: %.1f MHz - %s (%lu μs)\n", 
                      i+1, freq / 1e6, locked ? "锁定" : "未锁定", freq_end - freq_start);
      }
      
      unsigned long total_time = millis() - start_time;
      Serial.printf("📊 测试完成: 10次切换耗时 %lu ms，平均每次 %.1f ms\n", 
                    total_time, total_time / 10.0);
    }
  }
  
  delay(10); // 主循环延时
}
