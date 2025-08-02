#include "SI4732_Scanner.h"
#include <Wire.h>
#include <SI4735.h> // PU2CLR 库

static SI4735 radio;
static bool si_ready = false;

bool SI_init_AM_IF107()
{
    if (!si_ready)
    {
        // I2C
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        Wire.setClock(I2C_FREQ_HZ);

        // 设备地址（不同版本库方法名可能不同，二选一留一个）
        radio.getDeviceI2CAddress(SI_RST_PIN);

        radio.setup(SI_RST_PIN, 1); // 1=使用外部晶振（XOSCEN）

        // 进入 AM 模式 + 设定 IF
        radio.setAM(); 
        radio.setFrequency(SI_IF_kHz); // 设定 10.7 MHz（kHz 单位）

        // 可选：关静音/开 AGC（按需）
        // radio.setAudioMute(false);
        // radio.setAutomaticGainControl(true);

        si_ready = true;
    }
    // 简单校验：再读一次质量试探设备是否响应
    int16_t rssi, snr;
    return SI_getQuality(rssi, snr);
}

bool SI_getQuality(int16_t &rssi, int16_t &snr)
{
    if (!si_ready)
        return false;

    // PU2CLR 典型顺序：先请求“当前接收质量”，再分别读 RSSI/SNR
    radio.getCurrentReceivedSignalQuality();
    rssi = radio.getCurrentRSSI(); // dBµV
    snr = radio.getCurrentSNR();   // dB

    return true;
}

extern SI4735 radio;
extern bool si_ready;

// 快速切 AM 模式：不重复 I2C/复位，只改制式与频点
bool SI_set_mode_AM_IF107()
{
    if (!si_ready)
        return false;
    radio.setAM();                 // 或 radio.setAM(150, 30000);
    radio.setFrequency(SI_IF_kHz); // 10700 kHz
    return true;
}

// 快速切 FM 模式并调到 rf_hz（单位 Hz）
bool SI_set_mode_FM(uint32_t rf_hz)
{
    if (!si_ready)
        return false;
    radio.setFM();                    // 或 radio.setFM(6400, 10800);  // 6.4~108MHz
    radio.setFrequency(rf_hz / 10000); // 库是 kHz
    return true;
}
