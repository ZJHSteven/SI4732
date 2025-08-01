#include <Wire.h>
#include <SI4735.h>
#define RST_PIN 27
SI4735 radio;

void setup()
{
  Serial.begin(115200);
  Wire.begin(21, 22, 800000);

  if (radio.getDeviceI2CAddress(RST_PIN) == 0)
  {
    Serial.println("Si47xx 未响应");
    while (1)
      delay(1000);
  }

  radio.setup(RST_PIN, 0);
  radio.setFM(8800, 10800, 8800, 10); // 88~108 MHz
  radio.setAudioMode(SI473X_ANALOG_AUDIO);
  radio.setVolume(0); // 静音扫

  // —— 关键属性：SEEK 步进 = 100 kHz；放宽可接受频偏 —— //
  radio.setProperty(0x1402 /*FM_SEEK_FREQ_SPACING*/, 10);        // 100 kHz（单位10kHz）
  radio.setProperty(0x1403 /*FM_SEEK_TUNE_SNR_THRESHOLD*/, 3);   // dB
  radio.setProperty(0x1404 /*FM_SEEK_TUNE_RSSI_THRESHOLD*/, 20); // dBµV
  radio.setProperty(0x1108 /*FM_MAX_TUNE_ERROR*/, 50);           // 先设 ±50 kHz；必要时再加大到 60~75
}

void loop()
{
  // 从下边界开始向上 seek，收集所有停靠台一次
  uint16_t hits = 0;
  uint16_t f_start = 0, f_prev = 0;

  // 先把频点设到下边界，确保 SEEK 覆盖全带
  radio.setFrequency(8800);

  while (true)
  {
    // 向上 SEEK（不建议 wrap，避免死循环；若库不提供该参数，则自行检测回环）
    radio.seekStationUp();

    // 读取当前停靠频率与 RSQ
    uint16_t f10k = radio.getFrequency();
    radio.getCurrentReceivedSignalQuality();
    uint8_t rssi = radio.getCurrentRSSI();
    uint8_t snr = radio.getCurrentSNR();

    if (f10k == 0 || f10k == f_prev)
      break; // 库若在无台/到顶返回同频或 0，退出
    if (f_start == 0)
      f_start = f10k; // 记录首个停靠台
    else if (f10k == f_start)
      break; // 回到首台 => 一圈结束

    Serial.printf("%2u) %6.2f MHz  RSSI=%u  SNR=%u\n", ++hits, f10k / 100.0, rssi, snr);
    f_prev = f10k;

    // 若不想遍历全带，可在这里按命中数/上限退出
  }

  Serial.printf("SEEK DONE. total=%u\n", hits);
  while (1)
    delay(1000);
}
