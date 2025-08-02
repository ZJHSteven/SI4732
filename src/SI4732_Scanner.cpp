#include "SI4732_Scanner.h"

/* ═══════════════════════════════════════════════════════════════
 * SI4732 扫频模块实现
 * ═══════════════════════════════════════════════════════════════ */

SI4732_Scanner::SI4732_Scanner(uint8_t rst_pin, uint8_t sda_pin, uint8_t scl_pin, uint32_t i2c_freq)
    : rst_pin(rst_pin), sda_pin(sda_pin), scl_pin(scl_pin), i2c_freq(i2c_freq)
{
    initialized = false;
    silent_mode = false;
    current_rssi = 0;
    current_snr = 0;
    current_freq = 0;
    band_start = 8800;
    band_end = 10800;
}

bool SI4732_Scanner::init()
{
    debugPrint("🔧 初始化SI4732扫频模块...");
    
    // 初始化I2C
    Wire.begin(sda_pin, scl_pin, i2c_freq);
    delay(100);
    
    // 检查SI4732响应
    if (radio.getDeviceI2CAddress(rst_pin) == 0)
    {
        debugPrint("❌ SI4732未响应，初始化失败");
        return false;
    }
    
    // 设置SI4732
    radio.setup(rst_pin, 0);
    delay(100);
    
    // 设置音频模式为模拟输出
    radio.setAudioMode(SI473X_ANALOG_AUDIO);
    radio.setVolume(0); // 默认静音，避免扫频时的噪音
    
    initialized = true;
    debugPrint("✅ SI4732初始化完成");
    return true;
}

bool SI4732_Scanner::setFMMode(uint16_t band_start, uint16_t band_end, uint16_t freq_init, uint8_t freq_step)
{
    if (!initialized) return false;
    
    this->band_start = band_start;
    this->band_end = band_end;
    
    debugPrint("📻 设置FM模式: %.1f - %.1f MHz", band_start/100.0, band_end/100.0);
    
    // 设置FM波段
    radio.setFM(band_start, band_end, freq_init, freq_step);
    
    // 配置SEEK参数 - 优化搜台灵敏度和覆盖范围
    radio.setProperty(0x1402 /*FM_SEEK_FREQ_SPACING*/, 10);        // 100kHz步进
    radio.setProperty(0x1403 /*FM_SEEK_TUNE_SNR_THRESHOLD*/, 2);   // 降低SNR阈值到2dB
    radio.setProperty(0x1404 /*FM_SEEK_TUNE_RSSI_THRESHOLD*/, 15); // 降低RSSI阈值到15dBµV
    radio.setProperty(0x1108 /*FM_MAX_TUNE_ERROR*/, 75);           // 增大频偏容忍到±75kHz
    
    current_freq = freq_init;
    debugPrint("✅ FM模式设置完成");
    return true;
}

bool SI4732_Scanner::setAMMode(uint16_t band_start, uint16_t band_end, uint16_t freq_init, uint8_t freq_step)
{
    if (!initialized) return false;
    
    this->band_start = band_start;
    this->band_end = band_end;
    
    debugPrint("📻 设置AM模式: %d - %d kHz", band_start, band_end);
    
    // 设置AM波段
    radio.setAM(band_start, band_end, freq_init, freq_step);
    
    current_freq = freq_init;
    debugPrint("✅ AM模式设置完成");
    return true;
}

uint16_t SI4732_Scanner::seekStations(StationInfo* stations, uint16_t max_stations)
{
    if (!initialized || !stations || max_stations == 0) return 0;
    
    debugPrint("🔍 开始扫描电台 (范围: %.1f - %.1f MHz)...", band_start/100.0, band_end/100.0);
    
    uint16_t station_count = 0;
    uint16_t first_freq = 0;
    uint16_t prev_freq = 0;
    uint16_t start_freq = band_start;
    
    // 从波段起始位置开始，确保完整覆盖
    radio.setFrequency(start_freq);
    delay(100);
    
    debugPrint("📡 开始频率: %.2f MHz", start_freq/100.0);
    
    while (station_count < max_stations)
    {
        // 向上搜索下一个电台
        radio.seekStationUp();
        delay(100); // 增加等待时间确保搜索完成
        
        uint16_t freq = radio.getFrequency();
        
        debugPrint("🔍 搜索结果: %.2f MHz (prev: %.2f MHz)", freq/100.0, prev_freq/100.0);
        
        // 检查搜索结果
        if (freq == 0) {
            debugPrint("⚠️ 搜索返回频率为0，可能到达波段边界");
            break;
        }
        
        if (freq == prev_freq) {
            debugPrint("⚠️ 搜索返回相同频率，可能到达波段边界");
            break;
        }
        
        // 检查是否超出波段范围
        if (freq > band_end) {
            debugPrint("⚠️ 搜索超出波段上限 (%.1f MHz)", band_end/100.0);
            break;
        }
        
        // 记录第一个找到的电台，用于检测一圈扫描完成
        if (first_freq == 0) {
            first_freq = freq;
            debugPrint("📍 记录首个电台: %.2f MHz", first_freq/100.0);
        } else if (freq == first_freq) {
            debugPrint("🔄 扫描一圈完成，回到首个电台");
            break;
        }
        
        // 获取信号质量
        updateSignalQuality();
        
        // 保存电台信息
        stations[station_count].frequency = freq;
        stations[station_count].rssi = current_rssi;
        stations[station_count].snr = current_snr;
        stations[station_count].valid = isStationValid();
        
        if (!silent_mode) {
            debugPrint("📡 电台 %2d: %6.2f MHz, RSSI=%2d dBµV, SNR=%2d dB %s",
                      station_count + 1,
                      freq / 100.0,
                      current_rssi,
                      current_snr,
                      stations[station_count].valid ? "✓" : "✗");
        }
        
        station_count++;
        prev_freq = freq;
        
        delay(20); // 短暂延时
    }
    
    // 如果没有找到电台，尝试手动扫描
    if (station_count == 0) {
        debugPrint("🔍 SEEK未找到电台，尝试手动扫描...");
        station_count = manualScan(stations, max_stations);
    }
    
    debugPrint("✅ 扫描完成，找到 %d 个电台", station_count);
    return station_count;
}

bool SI4732_Scanner::seekNext()
{
    if (!initialized) return false;
    
    radio.seekStationUp();
    delay(50);
    
    uint16_t new_freq = radio.getFrequency();
    if (new_freq != current_freq && new_freq != 0) {
        current_freq = new_freq;
        updateSignalQuality();
        debugPrint("⬆️ 找到电台: %.2f MHz", current_freq / 100.0);
        return true;
    }
    return false;
}

bool SI4732_Scanner::seekPrev()
{
    if (!initialized) return false;
    
    radio.seekStationDown();
    delay(50);
    
    uint16_t new_freq = radio.getFrequency();
    if (new_freq != current_freq && new_freq != 0) {
        current_freq = new_freq;
        updateSignalQuality();
        debugPrint("⬇️ 找到电台: %.2f MHz", current_freq / 100.0);
        return true;
    }
    return false;
}

bool SI4732_Scanner::setFrequency(uint16_t freq_10khz)
{
    if (!initialized) return false;
    
    if (freq_10khz < band_start || freq_10khz > band_end) {
        debugPrint("⚠️ 频率超出波段范围: %.2f MHz", freq_10khz / 100.0);
        return false;
    }
    
    radio.setFrequency(freq_10khz);
    current_freq = freq_10khz;
    delay(30); // 等待频率稳定
    
    updateSignalQuality();
    debugPrint("📻 频率设置: %.2f MHz", current_freq / 100.0);
    return true;
}

uint16_t SI4732_Scanner::getFrequency()
{
    if (!initialized) return 0;
    
    current_freq = radio.getFrequency();
    return current_freq;
}

void SI4732_Scanner::updateSignalQuality()
{
    if (!initialized) return;
    
    radio.getCurrentReceivedSignalQuality();
    current_rssi = radio.getCurrentRSSI();
    current_snr = radio.getCurrentSNR();
}

bool SI4732_Scanner::isStationValid()
{
    // 简单的电台有效性判断：RSSI > 25 且 SNR > 5
    return (current_rssi > 25 && current_snr > 5);
}

void SI4732_Scanner::setVolume(uint8_t volume)
{
    if (!initialized) return;
    
    // SI4735音量范围是0-63
    if (volume > 63) volume = 63;
    
    radio.setVolume(volume);
    debugPrint("🔊 音量设置: %d", volume);
}

void SI4732_Scanner::mute(bool enable)
{
    if (!initialized) return;
    
    if (enable) {
        radio.setVolume(0);
        debugPrint("🔇 静音开启");
    } else {
        radio.setVolume(30); // 默认音量
        debugPrint("🔊 静音关闭");
    }
}

void SI4732_Scanner::setAudioMode(uint8_t mode)
{
    if (!initialized) return;
    
    radio.setAudioMode(mode);
    debugPrint("🎵 音频模式设置: %d", mode);
}

void SI4732_Scanner::printStatus()
{
    if (!initialized) {
        debugPrint("❌ SI4732未初始化");
        return;
    }
    
    updateSignalQuality();
    
    debugPrint("\n📊 SI4732状态报告:");
    debugPrint("  当前频率: %.2f MHz", current_freq / 100.0);
    debugPrint("  信号强度: %d dBµV", current_rssi);
    debugPrint("  信噪比: %d dB", current_snr);
    debugPrint("  电台状态: %s", isStationValid() ? "有效" : "无效");
    debugPrint("  波段范围: %.1f - %.1f MHz", band_start/100.0, band_end/100.0);
    debugPrint("  静默模式: %s", silent_mode ? "开启" : "关闭");
}

void SI4732_Scanner::printStationList(StationInfo* stations, uint16_t count)
{
    if (!stations || count == 0) {
        debugPrint("📻 无电台信息");
        return;
    }
    
    debugPrint("\n📻 电台列表 (共%d个):", count);
    debugPrint("序号   频率(MHz)   RSSI(dBµV)  SNR(dB)  状态");
    debugPrint("────────────────────────────────────────");
    
    for (uint16_t i = 0; i < count; i++) {
        debugPrint("%2d    %7.2f      %2d         %2d      %s",
                  i + 1,
                  stations[i].frequency / 100.0,
                  stations[i].rssi,
                  stations[i].snr,
                  stations[i].valid ? "✓" : "✗");
    }
    debugPrint("────────────────────────────────────────");
}

void SI4732_Scanner::debugPrint(const char* format, ...)
{
    if (silent_mode) return;
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.println(buffer);
}

// 手动扫描功能
uint16_t SI4732_Scanner::manualScan(StationInfo* stations, uint16_t max_stations)
{
    debugPrint("🔍 开始手动扫描 (%.1f - %.1f MHz)...", band_start/100.0, band_end/100.0);
    
    uint16_t station_count = 0;
    uint16_t scan_step = 20; // 200kHz步进
    
    for (uint16_t freq = band_start; freq <= band_end && station_count < max_stations; freq += scan_step) {
        // 设置频率
        radio.setFrequency(freq);
        delay(50); // 等待稳定
        
        // 获取信号质量
        updateSignalQuality();
        
        // 简单的信号检测：RSSI > 25 且 SNR > 8
        if (current_rssi > 25 && current_snr > 8) {
            stations[station_count].frequency = freq;
            stations[station_count].rssi = current_rssi;
            stations[station_count].snr = current_snr;
            stations[station_count].valid = true;
            
            debugPrint("📡 手动发现 %2d: %6.2f MHz, RSSI=%2d dBµV, SNR=%2d dB ✓",
                      station_count + 1,
                      freq / 100.0,
                      current_rssi,
                      current_snr);
            
            station_count++;
        }
    }
    
    debugPrint("✅ 手动扫描完成，找到 %d 个电台", station_count);
    return station_count;
}
