/*
 * 频率过滤功能测试示例
 * 
 * 演示如何使用新的频率过滤功能过滤掉指定频率的电台
 */

#include "SI4732_Scanner.h"

// 引脚定义
#define RST_PIN 4
#define SDA_PIN 21  
#define SCL_PIN 22

// 创建扫描器实例
SI4732_Scanner si_scanner(RST_PIN, SDA_PIN, SCL_PIN, 800000);
SI4732_Scanner::StationInfo stations[50];
uint16_t station_count = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("=== 频率过滤功能测试 ===");
    
    // 初始化SI4732
    if (!si_scanner.init()) {
        Serial.println("❌ SI4732初始化失败");
        return;
    }
    
    // 设置FM模式
    si_scanner.setFMMode(8750, 10850, 10000, 10);
    
    Serial.println("\n--- 测试1: 无过滤扫描 ---");
    testWithoutFilter();
    
    Serial.println("\n--- 测试2: 使用默认过滤列表 ---");
    testWithDefaultFilter();
    
    Serial.println("\n--- 测试3: 使用自定义过滤列表 ---");
    testWithCustomFilter();
    
    Serial.println("\n=== 测试完成 ===");
}

void loop() {
    // 主循环空闲
    delay(1000);
}

// 测试1: 无过滤扫描
void testWithoutFilter() {
    // 清除任何现有过滤
    si_scanner.clearFrequencyFilter();
    
    // 扫描电台
    station_count = si_scanner.seekStations(stations, 50);
    
    Serial.printf("找到 %d 个电台（无过滤）:\n", station_count);
    si_scanner.printStationList(stations, station_count);
}

// 测试2: 使用默认过滤列表
void testWithDefaultFilter() {
    // 设置默认过滤列表
    si_scanner.setDefaultFrequencyFilter();
    
    // 重新扫描电台
    station_count = si_scanner.seekStations(stations, 50);
    
    Serial.printf("找到 %d 个电台（使用默认过滤）:\n", station_count);
    si_scanner.printStationList(stations, station_count);
}

// 测试3: 使用自定义过滤列表
void testWithCustomFilter() {
    // 定义自定义过滤列表（只过滤几个特定频率）
    uint16_t custom_filter[] = {
        8860,  // 88.60 MHz
        9900,  // 99.00 MHz
        10460  // 104.60 MHz
    };
    
    uint16_t filter_count = sizeof(custom_filter) / sizeof(custom_filter[0]);
    si_scanner.setFrequencyFilter(custom_filter, filter_count);
    
    // 重新扫描电台
    station_count = si_scanner.seekStations(stations, 50);
    
    Serial.printf("找到 %d 个电台（自定义过滤3个频率）:\n", station_count);
    si_scanner.printStationList(stations, station_count);
}
