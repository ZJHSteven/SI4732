/*
 *  ESP32 + Si4732-A10   （PU2CLR SI4735 库 ≥ 2.1.4）
 *  功能：从 108 MHz 向下自动搜台，打印频率 / RSSI / SNR
 *  时序全部采用轮询，无任何中断线。
 *  -----------------------------------------------
 *  接线：
 *     ESP32      Si4732
 *     GPIO21 --- SDA
 *     GPIO22 --- SCL
 *     GPIO27 --- RST
 *               SEN → Vdd (I²C = 0x63，库可自动检测)
 *     32.768 kHz 晶体 → XOSC
 */

#include <Wire.h>
#include <SI4735.h>

#define RST_PIN 27 // 复位
SI4735 radio;

// ─────── 工具：等待 STCINT 置位（轮询）─────────
void waitForSTC()
{
  while (!radio.getTuneCompleteTriggered())
  {            // 轮询 STC 标志
    delay(20); // 20 ms 足够
  }
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(21, 22, 400000); // 400 kHz I²C

  /* 1. 自动侦测 I²C 地址（0x11 ↔ 0x63）
   *    此调用既返回地址，也把地址写进库内部，
   *    后面无需再 setDeviceI2CAddress()。    */
  if (radio.getDeviceI2CAddress(RST_PIN) == 0)
  {
    Serial.println("Si47xx 未响应，检查接线！");
    while (1)
      ; // 死等
  }

  /* 2. 上电到 FM，使用板上 32 kHz 晶体 */
  radio.setup(RST_PIN, 0);

  /* 3. 配置 FM 波段
   *    单位 = 10 kHz；6400=64 MHz，10800=108 MHz  */
  radio.setFM(6400, 10800, 6400, 10); // 从 108 MHz 起步
  radio.setSeekFmSpacing(10);          // 100 kHz 网格

  radio.setSeekFmRssiThreshold(10);    // dBµV，阈值可放宽
  radio.setSeekFmSNRThreshold(5);      // dB
}

void loop()
{
  // 只做一次：定到 108.00 MHz
  static bool tuned = false;
  if (!tuned)
  {
    radio.setFrequency(10800); // 单位=10kHz → 10800=108.00 MHz
    tuned = true;
  }

  // 轮询 RSQ（先请求，再读 RSSI/SNR）
  radio.getCurrentReceivedSignalQuality();
  uint8_t rssi = radio.getCurrentRSSI();
  uint8_t snr = radio.getCurrentSNR();
  Serial.printf("MEAS 108.00 MHz  RSSI=%u dBµV  SNR=%u dB\n", rssi, snr);

  delay(200);
}
