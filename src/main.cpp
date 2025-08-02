#include <Arduino.h>
#include "ADF4351_Controller.h"
#include "SI4732_Scanner.h"
#include "PathController.h"

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

// --- ESP32 ↔ SI4732 引脚映射 ---
constexpr uint8_t SI_RST = 27;   // SI4732复位引脚
constexpr uint8_t SI_SDA = 21;   // I2C数据引脚
constexpr uint8_t SI_SCL = 22;   // I2C时钟引脚

// --- 路径控制引脚 ---
constexpr uint8_t SWITCH_PIN = 26; // 数字开关控制引脚 (需要根据实际硬件设置)

// --- ADF4351 PLL 参数 ---
constexpr uint16_t R_DIV = 1; // R 分频器

// --- 可变射频频率 - 这个值可以在运行时动态修改 ---
uint32_t rfHz = 108000000UL; // 当前射频频率，默认108MHz (FM)

// --- 其他应用模块变量 ---
bool frequency_changed = false; // 频率改变标志
unsigned long last_update = 0; // 上次更新时间

// --- 工作模式 ---
enum WorkMode {
    MODE_FM_SEEK = 0,     // FM搜台模式
    MODE_AM_ANALYSIS = 1, // AM分析模式  
    MODE_ADF_SCAN = 2     // ADF快速扫频模式
};
WorkMode current_mode = MODE_FM_SEEK; // 默认FM搜台模式
// ───────────────────────────────────────────────────────────────── */

/* ========================= 模块实例化 ========================= */
ADF4351_Controller adf_controller(ADF_LE, ADF_CE, ADF_LD, ADF_SCK, ADF_MOSI, REF_Hz, IF_Hz, R_DIV);
SI4732_Scanner si_scanner(SI_RST, SI_SDA, SI_SCL, 800000);
PathController path_controller(SWITCH_PIN);

/* ========================= 电台信息存储 ========================= */
constexpr uint16_t MAX_STATIONS = 50; // 最大电台数量
SI4732_Scanner::StationInfo found_stations[MAX_STATIONS];
uint16_t station_count = 0;
int16_t current_station_index = -1; // 当前选中的电台索引

/* ========================= 函数声明 ========================= */
// 模式控制函数
void switchToFMSeekMode();
void switchToAMAnalysisMode(uint32_t rf_freq_hz);
void switchToADFScanMode();
void performFMSeek();
void analyzeFMStationsWithAM();

// 频率控制函数
bool setRfFrequency(uint32_t new_rf_hz);
uint32_t getRfFrequency();
void frequencyScan(uint32_t start_hz, uint32_t end_hz, uint32_t step_hz);

// 辅助函数
void printHelpMenu();
void printSystemStatus();
const char* getModeString();
void selectStation(int station_num);
void nextStation();
void prevStation();
void setManualFrequency(float freq_mhz);

// ADF扫频函数
void startADFScan();
void toggleADFScan();
void processADFScan();
void performSpeedTest();
void testADFOutput(float freq_mhz);

/* ========================= 模式控制函数 ========================= */
// 切换到FM搜台模式
void switchToFMSeekMode()
{
    current_mode = MODE_FM_SEEK;
    Serial.println("\n🔄 切换到FM搜台模式");
    
    // 切换到FM直通路径
    path_controller.switchToFMPath();
    
    // 设置SI4732为FM模式 - 扩展到108.5MHz确保覆盖108MHz
    si_scanner.setFMMode(8750, 10850, 8750, 10); // 87.5-108.5MHz
    si_scanner.setVolume(30); // 适中音量
    
    Serial.println("✅ FM搜台模式就绪");
}

// 切换到AM分析模式
void switchToAMAnalysisMode(uint32_t rf_freq_hz)
{
    current_mode = MODE_AM_ANALYSIS;
    Serial.printf("\n🔄 切换到AM分析模式 (RF: %.2f MHz)\n", rf_freq_hz / 1e6);
    
    // 切换到AM降频路径
    path_controller.switchToAMPath();
    
    // 设置ADF4351生成对应的本振频率
    adf_controller.setRfFrequency(rf_freq_hz);
    if (adf_controller.waitForLock(100)) {
        Serial.printf("✅ ADF4351锁定: LO=%.3f MHz\n", adf_controller.getCurrentLoFrequency() / 1e6);
    } else {
        Serial.println("⚠️  ADF4351未锁定");
    }
    
    // 设置SI4732为AM模式，接收10.7MHz中频
    si_scanner.setAMMode(1070, 1070, 1070, 1); // 10.7MHz中频
    si_scanner.setVolume(30); // 适中音量
    
    Serial.println("✅ AM分析模式就绪");
}

// 切换到ADF快速扫频模式
void switchToADFScanMode()
{
    current_mode = MODE_ADF_SCAN;
    Serial.println("\n🔄 切换到ADF快速扫频模式");
    
    // 切换到AM降频路径
    path_controller.switchToAMPath();
    
    // 设置SI4732为AM模式接收中频
    si_scanner.setAMMode(1070, 1070, 1070, 1);
    si_scanner.setVolume(0); // 静音扫频
    
    Serial.println("✅ ADF快速扫频模式就绪");
}

// 执行FM电台搜索
void performFMSeek()
{
    Serial.println("\n🔍 开始FM电台搜索...");
    si_scanner.setSilentMode(false); // 显示搜索过程
    
    station_count = si_scanner.seekStations(found_stations, MAX_STATIONS);
    
    if (station_count > 0) {
        Serial.printf("\n✅ 搜索完成，找到 %d 个电台\n", station_count);
        si_scanner.printStationList(found_stations, station_count);
        current_station_index = 0; // 选择第一个电台
        
        // 调谐到第一个电台
        si_scanner.setFrequency(found_stations[0].frequency);
        Serial.printf("📻 调谐到电台1: %.2f MHz\n", found_stations[0].frequency / 100.0);
    } else {
        Serial.println("❌ 未找到任何电台");
        current_station_index = -1;
    }
}

// 对找到的FM电台进行AM分析
void analyzeFMStationsWithAM()
{
    if (station_count == 0) {
        Serial.println("⚠️ 没有找到FM电台，请先执行FM搜索");
        return;
    }
    
    Serial.printf("\n🔬 开始对 %d 个FM电台进行AM分析...\n", station_count);
    
    for (uint16_t i = 0; i < station_count; i++) {
        uint32_t rf_freq_hz = found_stations[i].frequency * 10000UL; // 转换为Hz
        
        Serial.printf("\n--- 分析电台 %d: %.2f MHz ---\n", i + 1, rf_freq_hz / 1e6);
        
        // 切换到AM分析模式
        switchToAMAnalysisMode(rf_freq_hz);
        
        // 等待信号稳定
        delay(200);
        
        // 读取AM解调后的信号质量
        si_scanner.updateSignalQuality();
        uint8_t am_rssi = si_scanner.getRSSI();
        uint8_t am_snr = si_scanner.getSNR();
        
        Serial.printf("FM信号: RSSI=%d dBµV, SNR=%d dB\n", 
                     found_stations[i].rssi, found_stations[i].snr);
        Serial.printf("AM解调: RSSI=%d dBµV, SNR=%d dB\n", am_rssi, am_snr);
        
        // 简单的AM检测逻辑：如果AM解调后还有一定信号，可能包含AM调制
        if (am_rssi > 15 && am_snr > 3) {
            Serial.println("🔍 可能检测到AM调制成分!");
        } else {
            Serial.println("📻 纯FM信号");
        }
        
        delay(500); // 短暂停留
    }
    
    Serial.println("\n✅ AM分析完成");
    
    // 回到FM模式
    switchToFMSeekMode();
    if (current_station_index >= 0) {
        si_scanner.setFrequency(found_stations[current_station_index].frequency);
    }
}
/* ========================= 频率控制函数 ========================= */
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
  Serial.println("  SI4732 + ADF4351 双模解调系统启动");
  Serial.println("=======================================");
  
  // 初始化路径控制器
  path_controller.init();
  
  // 初始化ADF4351控制器
  adf_controller.init();
  adf_controller.enable();
  
  // 初始化SI4732扫频模块
  if (!si_scanner.init()) {
    Serial.println("❌ SI4732初始化失败，系统无法启动");
    while(1) delay(1000);
  }
  
  // 默认进入FM搜台模式
  switchToFMSeekMode();
  
  Serial.println("\n✅ 系统初始化完成");
  Serial.println("📝 当前模式: FM搜台模式");
  Serial.println("📝 输入 'help' 查看所有可用命令");
  Serial.println("� 输入 'seek' 开始FM电台搜索");
  
  last_update = millis();
}

/* ========================= 主循环 ========================= */
void loop()
{
  unsigned long current_time = millis();
  
  // 处理串口命令
  if (Serial.available())
  {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase(); // 转为小写便于比较
    
    // === 基本控制命令 ===
    if (command == "help")
    {
      printHelpMenu();
    }
    else if (command == "status")
    {
      printSystemStatus();
    }
    
    // === FM搜台相关命令 ===
    else if (command == "fm" || command == "fmmode")
    {
      switchToFMSeekMode();
    }
    else if (command == "seek" || command == "fmseek")
    {
      if (current_mode != MODE_FM_SEEK) {
        switchToFMSeekMode();
      }
      performFMSeek();
    }
    else if (command.startsWith("station ") || command.startsWith("st "))
    {
      int station_num = command.substring(command.indexOf(' ') + 1).toInt();
      selectStation(station_num);
    }
    else if (command == "next" || command == "n")
    {
      nextStation();
    }
    else if (command == "prev" || command == "p")
    {
      prevStation();
    }
    else if (command == "list" || command == "stations")
    {
      if (station_count > 0) {
        si_scanner.printStationList(found_stations, station_count);
      } else {
        Serial.println("📻 无电台信息，请先执行 'seek' 搜索");
      }
    }
    
    // === AM分析相关命令 ===
    else if (command == "am" || command == "ammode")
    {
      if (station_count > 0 && current_station_index >= 0) {
        uint32_t rf_freq = found_stations[current_station_index].frequency * 10000UL;
        switchToAMAnalysisMode(rf_freq);
      } else {
        Serial.println("⚠️ 请先选择一个FM电台");
      }
    }
    else if (command == "analyze" || command == "amanalyze")
    {
      analyzeFMStationsWithAM();
    }
    
    // === ADF测试命令 ===
    else if (command.startsWith("adftest "))
    {
      float freq_mhz = command.substring(8).toFloat();
      if (freq_mhz > 0) {
        testADFOutput(freq_mhz);
      }
    }
    else if (command == "adfon")
    {
      adf_controller.enable();
      Serial.println("🔧 ADF4351输出已启用");
    }
    else if (command == "adfoff")
    {
      adf_controller.disable();
      Serial.println("🔧 ADF4351输出已禁用");
    }
    
    // === ADF扫频相关命令 ===
    else if (command == "adf" || command == "adfscan")
    {
      switchToADFScanMode();
      startADFScan();
    }
    else if (command == "fastscan")
    {
      if (current_mode == MODE_ADF_SCAN) {
        toggleADFScan();
      } else {
        switchToADFScanMode();
        startADFScan();
      }
    }
    
    // === 频率设置命令 ===
    else if (command.startsWith("freq "))
    {
      float freq_mhz = command.substring(5).toFloat();
      if (freq_mhz > 0) {
        setManualFrequency(freq_mhz);
      }
    }
    else if (command.startsWith("rf "))
    {
      float freq_mhz = command.substring(3).toFloat();
      if (freq_mhz > 0) {
        setManualFrequency(freq_mhz);
      }
    }
    
    // === 音量控制命令 ===
    else if (command.startsWith("vol ") || command.startsWith("volume "))
    {
      int volume = command.substring(command.indexOf(' ') + 1).toInt();
      if (volume >= 0 && volume <= 63) {
        si_scanner.setVolume(volume);
        Serial.printf("� 音量设置为: %d\n", volume);
      }
    }
    else if (command == "mute")
    {
      si_scanner.mute(true);
    }
    else if (command == "unmute")
    {
      si_scanner.mute(false);
    }
    
    // === 路径控制命令 ===
    else if (command == "path1" || command == "fmpath")
    {
      path_controller.switchToFMPath();
      path_controller.printStatus();
    }
    else if (command == "path2" || command == "ampath")
    {
      path_controller.switchToAMPath();
      path_controller.printStatus();
    }
    
    // === 其他命令 ===
    else if (command == "speedtest")
    {
      performSpeedTest();
    }
    else if (command.length() > 0)
    {
      Serial.printf("❓ 未知命令: '%s'，输入 'help' 查看帮助\n", command.c_str());
    }
  }
  
  // ADF扫频模式的循环处理
  if (current_mode == MODE_ADF_SCAN) {
    processADFScan();
  }
  
  delay(10); // 主循环延时
}

/* ========================= 辅助函数实现 ========================= */
// 打印帮助菜单
void printHelpMenu()
{
  Serial.println("\n📖 SI4732+ADF4351双模解调系统 - 命令帮助");
  Serial.println("════════════════════════════════════════════════");
  
  Serial.println("\n🔧 基本控制:");
  Serial.println("  help            - 显示此帮助菜单");
  Serial.println("  status          - 显示系统状态");
  
  Serial.println("\n📻 FM搜台模式:");
  Serial.println("  fm, fmmode      - 切换到FM搜台模式");
  Serial.println("  seek, fmseek    - 开始FM电台搜索");
  Serial.println("  list, stations  - 显示找到的电台列表");
  Serial.println("  station <n>     - 选择第n个电台 (st <n>)");
  Serial.println("  next, n         - 下一个电台");
  Serial.println("  prev, p         - 上一个电台");
  
  Serial.println("\n🔬 AM分析模式:");
  Serial.println("  am, ammode      - 切换当前电台到AM分析模式");
  Serial.println("  analyze         - 对所有FM电台进行AM分析");
  
  Serial.println("\n⚡ ADF扫频模式:");
  Serial.println("  adf, adfscan    - 切换到ADF快速扫频模式");
  Serial.println("  fastscan        - 开启/暂停ADF扫频");
  
  Serial.println("\n🎛️ 频率/音量控制:");
  Serial.println("  freq <MHz>      - 手动设置频率 (rf <MHz>)");
  Serial.println("  vol <0-63>      - 设置音量");
  Serial.println("  mute/unmute     - 静音/取消静音");
  
  Serial.println("\n🔀 路径控制:");
  Serial.println("  path1, fmpath   - 切换到FM直通路径");
  Serial.println("  path2, ampath   - 切换到AM降频路径");
  
  Serial.println("\n🧪 测试功能:");
  Serial.println("  speedtest       - 频率切换速度测试");
  Serial.println("  adftest <MHz>   - 测试ADF4351输出指定频率");
  Serial.println("  adfon/adfoff    - 启用/禁用ADF4351输出");
  
  Serial.println("\n当前系统状态:");
  Serial.printf("  工作模式: %s\n", getModeString());
  Serial.printf("  信号路径: %s\n", path_controller.getPathName(path_controller.getCurrentPath()));
  if (station_count > 0) {
    Serial.printf("  已找到电台: %d个\n", station_count);
    if (current_station_index >= 0) {
      Serial.printf("  当前电台: #%d (%.2f MHz)\n", 
                    current_station_index + 1, 
                    found_stations[current_station_index].frequency / 100.0);
    }
  }
}

// 打印系统状态
void printSystemStatus()
{
  Serial.println("\n📊 系统状态报告");
  Serial.println("════════════════════════════════════════════════");
  
  Serial.printf("工作模式: %s\n", getModeString());
  
  // 路径控制状态
  path_controller.printStatus();
  
  // SI4732状态
  si_scanner.printStatus();
  
  // ADF4351状态
  adf_controller.printStatus();
  
  // 电台信息
  if (station_count > 0) {
    Serial.printf("\n📻 电台信息:\n");
    Serial.printf("  已找到: %d个电台\n", station_count);
    if (current_station_index >= 0) {
      Serial.printf("  当前选中: #%d - %.2f MHz\n", 
                    current_station_index + 1, 
                    found_stations[current_station_index].frequency / 100.0);
    }
  } else {
    Serial.println("\n📻 电台信息: 暂无 (请执行 'seek' 搜索)");
  }
}

// 获取当前模式字符串
const char* getModeString()
{
  switch (current_mode) {
    case MODE_FM_SEEK: return "FM搜台模式";
    case MODE_AM_ANALYSIS: return "AM分析模式";
    case MODE_ADF_SCAN: return "ADF扫频模式";
    default: return "未知模式";
  }
}

// 选择电台
void selectStation(int station_num)
{
  if (station_count == 0) {
    Serial.println("⚠️ 无电台信息，请先执行 'seek' 搜索");
    return;
  }
  
  if (station_num < 1 || station_num > station_count) {
    Serial.printf("⚠️ 电台编号无效，请输入 1-%d\n", station_count);
    return;
  }
  
  current_station_index = station_num - 1;
  
  if (current_mode == MODE_FM_SEEK) {
    si_scanner.setFrequency(found_stations[current_station_index].frequency);
  } else if (current_mode == MODE_AM_ANALYSIS) {
    uint32_t rf_freq = found_stations[current_station_index].frequency * 10000UL;
    switchToAMAnalysisMode(rf_freq);
  }
  
  Serial.printf("📻 选择电台 #%d: %.2f MHz\n", 
                station_num, found_stations[current_station_index].frequency / 100.0);
}

// 下一个电台
void nextStation()
{
  if (station_count == 0) {
    Serial.println("⚠️ 无电台信息，请先执行 'seek' 搜索");
    return;
  }
  
  if (current_station_index < 0) {
    current_station_index = 0;
  } else {
    current_station_index = (current_station_index + 1) % station_count;
  }
  
  selectStation(current_station_index + 1);
}

// 上一个电台
void prevStation()
{
  if (station_count == 0) {
    Serial.println("⚠️ 无电台信息，请先执行 'seek' 搜索");
    return;
  }
  
  if (current_station_index < 0) {
    current_station_index = station_count - 1;
  } else {
    current_station_index = (current_station_index - 1 + station_count) % station_count;
  }
  
  selectStation(current_station_index + 1);
}

// 手动设置频率
void setManualFrequency(float freq_mhz)
{
  if (freq_mhz < 0.1 || freq_mhz > 2000) {
    Serial.println("⚠️ 频率范围无效");
    return;
  }
  
  if (current_mode == MODE_FM_SEEK) {
    // FM模式：直接设置SI4732频率
    uint16_t freq_10khz = (uint16_t)(freq_mhz * 100);
    if (si_scanner.setFrequency(freq_10khz)) {
      Serial.printf("📻 FM频率设置: %.2f MHz\n", freq_mhz);
    }
  } else {
    // AM模式：设置ADF4351的RF频率
    uint32_t freq_hz = (uint32_t)(freq_mhz * 1e6);
    switchToAMAnalysisMode(freq_hz);
  }
}

// ADF扫频相关变量
static bool adf_scan_active = false;
static unsigned long adf_scan_timer = 0;
static unsigned long adf_scan_start_time = 0;
static uint32_t adf_scan_start_freq = 88000000UL;  // 88MHz
static uint32_t adf_scan_end_freq = 108000000UL;   // 108MHz
static uint32_t adf_scan_step = 100000UL;          // 100kHz
static uint32_t adf_current_scan_freq = 88000000UL;
static unsigned long adf_scan_interval = 15;       // 15ms
static bool adf_scan_direction = true;             // true=向上，false=向下
static uint32_t adf_scan_count = 0;

// 开始ADF扫频
void startADFScan()
{
  adf_scan_active = true;
  adf_current_scan_freq = adf_scan_start_freq;
  adf_scan_direction = true;
  adf_scan_timer = millis();
  adf_scan_count = 0;
  adf_scan_start_time = millis();
  
  adf_controller.setSilentMode(true); // 静默模式
  si_scanner.setSilentMode(true);
  
  Serial.printf("🚀 开始ADF扫频: %.1f → %.1f MHz (步进%.0f kHz，间隔%lu ms)\n", 
                adf_scan_start_freq / 1e6, adf_scan_end_freq / 1e6, 
                adf_scan_step / 1e3, adf_scan_interval);
}

// 切换ADF扫频状态
void toggleADFScan()
{
  adf_scan_active = !adf_scan_active;
  Serial.printf("🔄 ADF扫频 %s\n", adf_scan_active ? "已启用" : "已暂停");
  
  if (adf_scan_active) {
    adf_scan_timer = millis();
    adf_controller.setSilentMode(true);
    si_scanner.setSilentMode(true);
  } else {
    adf_controller.setSilentMode(false);
    si_scanner.setSilentMode(false);
  }
}

// 处理ADF扫频
void processADFScan()
{
  if (!adf_scan_active) return;
  
  unsigned long current_time = millis();
  
  if (current_time - adf_scan_timer >= adf_scan_interval) {
    // 执行扫频步进
    adf_controller.setRfFrequency(adf_current_scan_freq);
    adf_scan_count++;
    
    // 计算下一个频率
    if (adf_scan_direction) { // 向上扫频
      adf_current_scan_freq += adf_scan_step;
      if (adf_current_scan_freq >= adf_scan_end_freq) {
        adf_current_scan_freq = adf_scan_end_freq;
        adf_scan_direction = false; // 切换为向下扫频
        
        unsigned long elapsed = current_time - adf_scan_start_time;
        Serial.printf("⬆️  向上扫频完成: %.1f秒，%u步\n", elapsed / 1000.0, adf_scan_count);
        adf_scan_count = 0;
        adf_scan_start_time = current_time;
      }
    } else { // 向下扫频
      adf_current_scan_freq -= adf_scan_step;
      if (adf_current_scan_freq <= adf_scan_start_freq) {
        adf_current_scan_freq = adf_scan_start_freq;
        adf_scan_direction = true; // 切换为向上扫频
        
        unsigned long elapsed = current_time - adf_scan_start_time;
        Serial.printf("⬇️  向下扫频完成: %.1f秒，%u步\n", elapsed / 1000.0, adf_scan_count);
        adf_scan_count = 0;
        adf_scan_start_time = current_time;
      }
    }
    
    adf_scan_timer = current_time;
  }
}

// 测试ADF4351输出
void testADFOutput(float freq_mhz)
{
  Serial.printf("🔧 测试ADF4351输出: %.2f MHz\n", freq_mhz);
  
  uint32_t freq_hz = (uint32_t)(freq_mhz * 1e6);
  
  // 强制启用ADF4351
  adf_controller.enable();
  
  // 设置RF频率（这会计算对应的本振频率）
  if (adf_controller.setRfFrequency(freq_hz)) {
    Serial.printf("📡 设置RF频率: %.3f MHz\n", freq_hz / 1e6);
    Serial.printf("📡 计算LO频率: %.3f MHz\n", adf_controller.getCurrentLoFrequency() / 1e6);
    
    // 等待锁定
    if (adf_controller.waitForLock(200)) {
      Serial.println("✅ ADF4351已锁定 - 应该可以在示波器上看到信号");
      Serial.println("📊 检查以下频率:");
      Serial.printf("   - LO输出: %.3f MHz\n", adf_controller.getCurrentLoFrequency() / 1e6);
      Serial.printf("   - RF目标: %.3f MHz\n", freq_hz / 1e6);
      Serial.printf("   - 中频差: %.3f MHz\n", adf_controller.getIfFrequency() / 1e6);
    } else {
      Serial.println("❌ ADF4351未锁定 - 检查硬件连接");
    }
    
    // 显示详细状态
    adf_controller.setSilentMode(false);
    adf_controller.printStatus();
  } else {
    Serial.println("❌ 频率设置失败");
  }
}

// 性能测试
void performSpeedTest()
{
  Serial.println("🔬 开始频率切换速度测试...");
  
  // 保存当前模式
  WorkMode saved_mode = current_mode;
  
  // 临时切换到AM模式进行测试
  switchToAMAnalysisMode(100000000UL); // 100MHz
  
  uint32_t test_freqs[] = {88000000UL, 95000000UL, 102000000UL, 108000000UL};
  unsigned long start_time = millis();
  
  adf_controller.setSilentMode(true);
  
  for (int i = 0; i < 10; i++) {
    uint32_t freq = test_freqs[i % 4];
    unsigned long freq_start = micros();
    
    adf_controller.setRfFrequency(freq);
    bool locked = adf_controller.waitForLock(50);
    
    unsigned long freq_end = micros();
    Serial.printf("  第%d次: %.1f MHz - %s (%lu μs)\n", 
                  i+1, freq / 1e6, locked ? "锁定" : "未锁定", freq_end - freq_start);
  }
  
  unsigned long total_time = millis() - start_time;
  Serial.printf("📊 测试完成: 10次切换耗时 %lu ms，平均每次 %.1f ms\n", 
                total_time, total_time / 10.0);
  
  adf_controller.setSilentMode(false);
  
  // 恢复原来的模式
  if (saved_mode == MODE_FM_SEEK) {
    switchToFMSeekMode();
  }
}
