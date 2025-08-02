#ifndef PATH_CONTROLLER_H
#define PATH_CONTROLLER_H

#include <Arduino.h>

/* ═══════════════════════════════════════════════════════════════
 * 路径控制模块 - 管理FM直通路径和AM降频路径切换
 * ═══════════════════════════════════════════════════════════════ */

enum SignalPath {
    PATH_FM_DIRECT = 0,    // 路径1: FM直通路径 (RF直接进入SI4732)
    PATH_AM_DOWNCONV = 1   // 路径2: AM降频路径 (RF经ADF4351降频至10.7MHz后进入SI4732)
};

class PathController
{
public:
    /* ───────────────────────── 构造函数 ───────────────────────── */
    PathController(uint8_t switch_pin);
    
    /* ───────────────────────── 初始化与配置 ───────────────────────── */
    void init();                                   // 初始化路径控制
    
    /* ───────────────────────── 路径切换 ───────────────────────── */
    void setPath(SignalPath path);                 // 设置信号路径
    SignalPath getCurrentPath() { return current_path; }
    
    /* ───────────────────────── 便捷方法 ───────────────────────── */
    void switchToFMPath();                         // 切换到FM直通路径
    void switchToAMPath();                         // 切换到AM降频路径
    bool isFMPath() { return current_path == PATH_FM_DIRECT; }
    bool isAMPath() { return current_path == PATH_AM_DOWNCONV; }
    
    /* ───────────────────────── 状态与调试 ───────────────────────── */
    void printStatus();                            // 打印当前路径状态
    const char* getPathName(SignalPath path);      // 获取路径名称
    
    /* ───────────────────────── 静默模式 ───────────────────────── */
    void setSilentMode(bool enable) { silent_mode = enable; }
    bool getSilentMode() { return silent_mode; }

private:
    /* ───────────────────────── 私有成员变量 ───────────────────────── */
    uint8_t switch_pin;            // 数字开关控制引脚
    SignalPath current_path;       // 当前信号路径
    bool silent_mode;              // 静默模式
    bool initialized;              // 初始化状态
    
    /* ───────────────────────── 私有辅助函数 ───────────────────────── */
    void debugPrint(const char* format, ...);     // 调试输出
};

#endif // PATH_CONTROLLER_H
