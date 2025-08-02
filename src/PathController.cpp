#include "PathController.h"

/* ═══════════════════════════════════════════════════════════════
 * 路径控制模块实现
 * ═══════════════════════════════════════════════════════════════ */

PathController::PathController(uint8_t switch_pin)
    : switch_pin(switch_pin)
{
    current_path = PATH_FM_DIRECT;  // 默认FM直通路径
    silent_mode = false;
    initialized = false;
}

void PathController::init()
{
    debugPrint("🔧 初始化路径控制模块...");
    
    // 设置开关引脚为输出模式
    pinMode(switch_pin, OUTPUT);
    
    // 默认设置为FM直通路径
    setPath(PATH_FM_DIRECT);
    
    initialized = true;
    debugPrint("✅ 路径控制模块初始化完成");
}

void PathController::setPath(SignalPath path)
{
    if (!initialized) {
        debugPrint("⚠️ 路径控制器未初始化");
        return;
    }
    
    current_path = path;
    
    // 设置数字开关
    // 0 = FM直通路径, 1 = AM降频路径
    digitalWrite(switch_pin, (path == PATH_AM_DOWNCONV) ? HIGH : LOW);
    
    debugPrint("🔄 路径切换: %s (引脚%d = %d)", 
              getPathName(path), 
              switch_pin, 
              (path == PATH_AM_DOWNCONV) ? 1 : 0);
    
    // 短暂延时确保开关稳定
    delay(10);
}

void PathController::switchToFMPath()
{
    setPath(PATH_FM_DIRECT);
}

void PathController::switchToAMPath()
{
    setPath(PATH_AM_DOWNCONV);
}

void PathController::printStatus()
{
    if (!initialized) {
        debugPrint("❌ 路径控制器未初始化");
        return;
    }
    
    debugPrint("\n🔀 路径控制状态:");
    debugPrint("  控制引脚: %d", switch_pin);
    debugPrint("  当前路径: %s", getPathName(current_path));
    debugPrint("  引脚状态: %s", (current_path == PATH_AM_DOWNCONV) ? "HIGH (1)" : "LOW (0)");
    debugPrint("  静默模式: %s", silent_mode ? "开启" : "关闭");
}

const char* PathController::getPathName(SignalPath path)
{
    switch (path) {
        case PATH_FM_DIRECT:
            return "FM直通路径 (RF→SI4732)";
        case PATH_AM_DOWNCONV:
            return "AM降频路径 (RF→ADF4351→10.7MHz→SI4732)";
        default:
            return "未知路径";
    }
}

void PathController::debugPrint(const char* format, ...)
{
    if (silent_mode) return;
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.println(buffer);
}
