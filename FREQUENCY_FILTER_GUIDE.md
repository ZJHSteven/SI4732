# 频率过滤功能使用说明

## 功能概述
新增的频率过滤功能允许您过滤掉指定频率的电台，只保留其他频率的电台进行显示和操作。

## 主要功能

### 1. 设置自定义频率过滤列表
```cpp
// 定义要过滤的频率列表（单位：10kHz）
uint16_t filter_list[] = {
    8860,  // 88.60 MHz
    10460, // 104.60 MHz
    9900,  // 99.00 MHz
    // ... 更多频率
};

// 设置过滤列表
uint16_t filter_count = sizeof(filter_list) / sizeof(filter_list[0]);
si_scanner.setFrequencyFilter(filter_list, filter_count);
```

### 2. 使用预设的默认过滤列表
```cpp
// 使用基于用户数据的默认过滤列表
si_scanner.setDefaultFrequencyFilter();
```

### 3. 清除频率过滤
```cpp
// 清除所有频率过滤，显示所有电台
si_scanner.clearFrequencyFilter();
```

## 默认过滤列表
默认过滤列表包含以下频率：

**第一批过滤频率：**
- 88.60 MHz (RSSI: 50 dBµV)
- 104.60 MHz (RSSI: 46 dBµV)  
- 99.00 MHz (RSSI: 45 dBµV)
- 87.80 MHz (RSSI: 38 dBµV)
- 87.90 MHz (RSSI: 36 dBµV)
- 88.70 MHz (RSSI: 32 dBµV)
- 98.90 MHz (RSSI: 32 dBµV)
- 98.80 MHz (RSSI: 25 dBµV)
- 99.20 MHz (RSSI: 23 dBµV)
- 87.60 MHz (RSSI: 22 dBµV)
- 90.10 MHz (RSSI: 22 dBµV)
- 104.20 MHz (RSSI: 22 dBµV)
- 97.40 MHz (RSSI: 21 dBµV)
- 107.90 MHz (RSSI: 19 dBµV)
- 103.30 MHz (RSSI: 18 dBµV)
- 89.90 MHz (RSSI: 17 dBµV)

**第二批新增过滤频率：**
- 88.50 MHz (RSSI: 36 dBµV)
- 88.60 MHz (RSSI: 29 dBµV) - 已在第一批中
- 87.80 MHz (RSSI: 21 dBµV) - 已在第一批中  
- 99.10 MHz (RSSI: 12 dBµV)
- 88.30 MHz (RSSI: 5 dBµV)

**总计：19个过滤频率**

## 工作流程
1. **扫描电台**: 使用 `seekStations()` 正常扫描所有电台
2. **RSSI排序**: 自动按信号强度从高到低排序
3. **频率过滤**: 如果设置了过滤列表，自动过滤掉指定频率的电台
4. **返回结果**: 返回过滤后的电台列表

## 使用示例
```cpp
// 初始化扫描器
SI4732_Scanner si_scanner(RST_PIN, SDA_PIN, SCL_PIN, 800000);
si_scanner.init();
si_scanner.setFMMode(8750, 10850, 10000, 10);

// 设置默认频率过滤
si_scanner.setDefaultFrequencyFilter();

// 扫描电台（自动应用过滤）
SI4732_Scanner::StationInfo stations[50];
uint16_t count = si_scanner.seekStations(stations, 50);

// 显示过滤后的电台列表
si_scanner.printStationList(stations, count);
```

## 技术细节
- **频率匹配精度**: ±10kHz误差范围
- **最大过滤频率数**: 50个
- **内存管理**: 自动处理临时数组的分配和释放
- **性能影响**: 过滤操作在扫描完成后执行，不影响扫描性能

## 注意事项
1. 频率过滤在扫描完成后应用，不会影响SI4732的硬件扫描过程
2. 过滤后的电台列表仍然按RSSI排序
3. 如果所有电台都被过滤，将返回空列表
4. 可以随时更改或清除过滤设置
