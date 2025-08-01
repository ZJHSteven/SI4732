# SI4732 + ADF4351 双模解调系统使用说明

## 系统概述

本系统集成了SI4732数字调谐芯片和ADF4351宽带合成器，实现了FM直通解调和AM降频解调两种工作模式：

- **路径1 (FM直通路径)**: RF信号直接进入SI4732进行FM解调
- **路径2 (AM降频路径)**: RF信号经ADF4351降频至10.7MHz后进入SI4732进行AM解调

## 硬件连接

### ESP32 ↔ ADF4351
- LE (Latch Enable): GPIO 5
- SCK (Serial Clock): GPIO 18  
- MOSI (Master Out Slave In): GPIO 23
- CE (Chip Enable): GPIO 32
- LD (Lock Detect): GPIO 33

### ESP32 ↔ SI4732
- RST (Reset): GPIO 27
- SDA (I2C Data): GPIO 21
- SCL (I2C Clock): GPIO 22

### 路径控制
- 数字开关控制引脚: GPIO 26 (需要根据实际硬件调整)

## 工作模式

### 1. FM搜台模式 (默认)
- 使用SI4732的硬件SEEK功能搜索88-108MHz FM电台
- 支持电台列表管理和快速切换
- 直接FM解调，音质最佳

### 2. AM分析模式  
- 对找到的FM电台进行AM成分分析
- 通过ADF4351将RF信号降频至10.7MHz中频
- 使用SI4732的AM解调功能检测可能的AM调制成分

### 3. ADF快速扫频模式
- 高速扫描88-108MHz频段(可调整)
- 通过10.7MHz中频路径快速检测信号
- 用于快速频谱分析

## 常用命令

### 基本控制
```
help            - 显示帮助菜单
status          - 显示系统状态
```

### FM搜台操作
```
fm              - 切换到FM搜台模式  
seek            - 开始搜索FM电台
list            - 显示电台列表
station 3       - 选择第3个电台
next            - 下一个电台
prev            - 上一个电台
```

### AM分析操作
```
am              - 将当前电台切换到AM分析模式
analyze         - 对所有FM电台进行AM分析
```

### ADF扫频操作
```
adf             - 切换到ADF扫频模式
fastscan        - 开启/暂停快速扫频
```

### 频率和音量控制
```
freq 101.5      - 设置频率为101.5MHz
vol 30          - 设置音量为30 (0-63)
mute            - 静音
unmute          - 取消静音
```

### 路径控制
```
path1           - 切换到FM直通路径
path2           - 切换到AM降频路径
```

## 使用流程示例

### 基本FM收听流程
1. 系统启动后自动进入FM搜台模式
2. 输入 `seek` 搜索所有FM电台
3. 输入 `list` 查看找到的电台
4. 输入 `station 5` 选择第5个电台
5. 使用 `next`/`prev` 切换电台

### AM分析流程
1. 先执行FM搜台获得电台列表
2. 输入 `analyze` 对所有电台进行AM分析
3. 或输入 `station 3` 选择特定电台，再输入 `am` 进行单独分析

### 手动频率设置
1. 输入 `freq 95.5` 设置为95.5MHz
2. 在FM模式下直接调谐SI4732
3. 在AM模式下通过ADF4351生成对应本振

## 技术参数

- **频率范围**: 88-108MHz (FM频段)
- **频率分辨率**: 10kHz (SI4732) / 1kHz (ADF4351)
- **中频**: 10.7MHz
- **参考时钟**: 50MHz
- **I2C速度**: 800kHz
- **扫频速度**: 约15ms/步 (ADF模式)

## 注意事项

1. **路径切换**: 在不同模式间切换时，系统会自动控制信号路径
2. **静默模式**: 扫频时会自动启用静默模式减少串口输出
3. **频率范围**: 确保设置的频率在合理范围内，避免超出芯片规格
4. **硬件连接**: 数字开关控制引脚需要根据实际硬件电路调整

## 故障排除

### SI4732未响应
- 检查I2C连接(SDA/SCL)
- 检查复位引脚连接
- 确认电源供电正常

### ADF4351未锁定
- 检查参考时钟信号
- 验证SPI连接(LE/SCK/MOSI)
- 检查CE使能信号

### 电台搜索失败
- 确认天线连接
- 检查RF信号路径
- 调整搜索灵敏度参数
