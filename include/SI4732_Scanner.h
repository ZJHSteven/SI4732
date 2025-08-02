#ifndef SI4732_SCANNER_H
#define SI4732_SCANNER_H

#include <Arduino.h>
#include <Wire.h>
#include <SI4735.h>

/* ═══════════════════════════════════════════════════════════════
 * SI4732 扫频模块 - 负责FM电台搜索和解调控制
 * ═══════════════════════════════════════════════════════════════ */

class SI4732_Scanner
{
public:
    /* ───────────────────────── 构造函数 ───────────────────────── */
    SI4732_Scanner(uint8_t rst_pin, uint8_t sda_pin, uint8_t scl_pin, uint32_t i2c_freq = 800000);
    
    /* ───────────────────────── 初始化与配置 ───────────────────────── */
    bool init();                                    // 初始化SI4732
    bool setFMMode(uint16_t band_start = 8800,     // 设置FM模式
                   uint16_t band_end = 10800, 
                   uint16_t freq_init = 8800, 
                   uint8_t freq_step = 10);
    bool setAMMode(uint16_t band_start = 520,      // 设置AM模式
                   uint16_t band_end = 1710, 
                   uint16_t freq_init = 520, 
                   uint8_t freq_step = 10);
    
    /* ───────────────────────── 扫频功能 ───────────────────────── */
    struct StationInfo {
        uint16_t frequency;     // 频率 (10kHz单位)
        uint8_t rssi;          // 信号强度
        uint8_t snr;           // 信噪比
        bool valid;            // 是否有效
    };
    
    uint16_t seekStations(StationInfo* stations, uint16_t max_stations); // 扫描所有电台
    bool seekNext();                               // 搜索下一个电台
    bool seekPrev();                               // 搜索上一个电台
    bool setFrequency(uint16_t freq_10khz);        // 设置频率
    uint16_t getFrequency();                       // 获取当前频率
    
    /* ───────────────────────── 信号质量 ───────────────────────── */
    void updateSignalQuality();                    // 更新信号质量
    uint8_t getRSSI() { return current_rssi; }
    uint8_t getSNR() { return current_snr; }
    bool isStationValid();                         // 判断当前台是否有效
    
    /* ───────────────────────── 音频控制 ───────────────────────── */
    void setVolume(uint8_t volume);                // 设置音量 (0-63)
    void mute(bool enable);                        // 静音控制
    void setAudioMode(uint8_t mode);               // 设置音频模式
    
    /* ───────────────────────── 状态与调试 ───────────────────────── */
    void printStatus();                            // 打印当前状态
    void printStationList(StationInfo* stations, uint16_t count); // 打印电台列表
    bool isInitialized() { return initialized; }
    
    /* ───────────────────────── 静默模式 ───────────────────────── */
    void setSilentMode(bool enable) { silent_mode = enable; }
    bool getSilentMode() { return silent_mode; }

private:
    /* ───────────────────────── 私有成员变量 ───────────────────────── */
    SI4735 radio;                  // SI4735库实例
    uint8_t rst_pin;               // 复位引脚
    uint8_t sda_pin;               // I2C数据引脚
    uint8_t scl_pin;               // I2C时钟引脚
    uint32_t i2c_freq;             // I2C频率
    
    bool initialized;              // 初始化状态
    bool silent_mode;              // 静默模式
    
    uint8_t current_rssi;          // 当前RSSI
    uint8_t current_snr;           // 当前SNR
    uint16_t current_freq;         // 当前频率
    
    uint16_t band_start;           // 波段起始频率
    uint16_t band_end;             // 波段结束频率
    
    /* ───────────────────────── 私有辅助函数 ───────────────────────── */
    void debugPrint(const char* format, ...);     // 调试输出
    uint16_t manualScan(StationInfo* stations, uint16_t max_stations); // 手动扫描
};

#endif // SI4732_SCANNER_H
