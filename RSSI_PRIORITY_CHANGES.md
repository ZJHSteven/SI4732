# RSSI优先级修改说明

## 修改概述
将系统从以SNR优先改为以RSSI优先，提高对信号强度的敏感性。

## 具体修改内容

### 1. SI4732_Scanner.cpp - setFMMode()函数
**位置**: 第59-62行  
**修改前**:
```cpp
// 配置SEEK参数 - 优化搜台灵敏度和覆盖范围
radio.setProperty(0x1403 /*FM_SEEK_TUNE_SNR_THRESHOLD*/, 2);   // 降低SNR阈值到2dB
radio.setProperty(0x1404 /*FM_SEEK_TUNE_RSSI_THRESHOLD*/, 15); // 降低RSSI阈值到15dBµV
```

**修改后**:
```cpp
// 配置SEEK参数 - 以RSSI为主要判断标准，SNR为辅助
radio.setProperty(0x1404 /*FM_SEEK_TUNE_RSSI_THRESHOLD*/, 12); // 降低RSSI阈值到12dBµV (主要判断)
radio.setProperty(0x1403 /*FM_SEEK_TUNE_SNR_THRESHOLD*/, 1);   // 进一步降低SNR阈值到1dB (辅助判断)
```

### 2. SI4732_Scanner.cpp - isStationValid()函数
**位置**: 第243-246行  
**修改前**:
```cpp
bool SI4732_Scanner::isStationValid()
{
    // 简单的电台有效性判断：RSSI > 25 且 SNR > 5
    return (current_rssi > 25 && current_snr > 5);
}
```

**修改后**:
```cpp
bool SI4732_Scanner::isStationValid()
{
    // 简单的电台有效性判断：RSSI优先，RSSI > 20 或者 (RSSI > 15 且 SNR > 3)
    return (current_rssi > 20 || (current_rssi > 15 && current_snr > 3));
}
```

### 3. SI4732_Scanner.cpp - manualScan()函数
**位置**: 第348-350行  
**修改前**:
```cpp
// 简单的信号检测：RSSI > 25 且 SNR > 8
if (current_rssi > 25 && current_snr > 8) {
```

**修改后**:
```cpp
// 简单的信号检测：RSSI优先，RSSI > 20 或者 (RSSI > 15 且 SNR > 5)
if (current_rssi > 20 || (current_rssi > 15 && current_snr > 5)) {
```

### 4. main.cpp - AM检测逻辑
**位置**: 第194-195行  
**修改前**:
```cpp
// 简单的AM检测逻辑：如果AM解调后还有一定信号，可能包含AM调制
if (am_rssi > 15 && am_snr > 3) {
```

**修改后**:
```cpp
// 简单的AM检测逻辑：RSSI优先，如果RSSI足够强或者有一定信号质量，可能包含AM调制
if (am_rssi > 12 || (am_rssi > 8 && am_snr > 2)) {
```

### 5. 新增电台列表RSSI排序功能
**文件**: SI4732_Scanner.h 和 SI4732_Scanner.cpp  
**新增函数**:
```cpp
void sortStationsByRSSI(StationInfo* stations, uint16_t count); // 按RSSI排序电台列表
```

**实现位置**: SI4732_Scanner.cpp 第375-395行  
**功能**: 
- 使用冒泡排序算法，按RSSI从高到低排序
- 在`seekStations()`和`manualScan()`完成后自动调用
- 确保信号最强的电台始终排在列表前面

## 修改效果

### 优先级调整
- **原来**: SNR和RSSI必须同时满足条件 (AND逻辑)
- **现在**: RSSI满足即可，或RSSI稍低但有基本SNR (OR逻辑)

### 电台列表排序
- **新增功能**: 所有扫描到的电台按RSSI从高到低自动排序
- **排序时机**: 在`seekStations()`和`manualScan()`完成后自动执行
- **排序算法**: 使用冒泡排序，确保信号最强的电台排在列表前面

### 阈值降低
- **FM_SEEK_TUNE_RSSI_THRESHOLD**: 15dBµV → 12dBµV
- **FM_SEEK_TUNE_SNR_THRESHOLD**: 2dB → 1dB  
- **电台有效性**: RSSI > 25 → RSSI > 20 或 (RSSI > 15 && SNR > 3)
- **手动扫描**: RSSI > 25 → RSSI > 20 或 (RSSI > 15 && SNR > 5)
- **AM检测**: RSSI > 15 → RSSI > 12 或 (RSSI > 8 && SNR > 2)

### 预期改进
1. **更灵敏的电台检测**: 能够检测到更多信号较弱但RSSI可接受的电台
2. **优先信号强度**: 即使SNR不理想，只要RSSI足够强就认为是有效电台
3. **更宽容的搜索**: 降低搜索门槛，避免遗漏有效信号
4. **保持质量平衡**: 通过OR逻辑保证在RSSI不够强时仍要求基本的SNR
5. **智能电台排序**: 自动按信号强度排序，最强信号的电台排在前面，便于用户优先选择

## 注意事项
- 修改后可能会检测到更多噪声较大的电台，但信号强度会更优先
- 建议在实际环境中测试调整具体的阈值参数
- 如需进一步调整，可以修改各个函数中的RSSI和SNR阈值数值
