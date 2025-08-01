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
  radio.setFM(6400, 10800, 8800, 10);  // 从 88 MHz 起步
  radio.setSeekFmSpacing(10);          // 100 kHz 网格

  radio.setSeekFmRssiThreshold(10);    // dBµV，阈值可放宽
  radio.setSeekFmSNRThreshold(5);      // dB
}

void loop()
{
  /* 发一次向下搜台命令（SEEK DOWN） */
  radio.seekStationDown();

  /* 轮询等待 STC 完成标志 */
  waitForSTC();

  /* 读取当前频道的信号质量 —— 必须先发 RSQ 查询 */
  radio.getCurrentReceivedSignalQuality(); // :contentReference[oaicite:0]{index=0}
  uint8_t rssi = radio.getCurrentRSSI();
  uint8_t snr = radio.getCurrentSNR();
  float freq = radio.getFrequency() / 100.0; // MHz

  Serial.printf("LOCK  %6.2f MHz   RSSI=%u dBµV   SNR=%u dB\n",
                freq, rssi, snr);

  /* 如果已到 64 MHz 带端，任务结束 */
  if (radio.getBandLimit())
  {
    Serial.println("扫完整个 FM 波段，退出。");
    while (1)
      delay(1000);
  }

  delay(50); // 小息，便于串口刷新
}
