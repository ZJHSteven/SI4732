/***********************************************************************
 *  Si4732 自动搜台 + 调制判别 + AGC（无中断版，库 ≥2.1.4）
 **********************************************************************/
#include <Wire.h>
#include <SI4735.h>

SI4735 radio;

/* ─────── 引脚定义 ────────────────────────────────────────────── */
#define PIN_RST 27
#define AUDIO_PEAK_PIN 36 // ADC1_CH0 (VP)
#define SPEAKER_MUTE 4

#define FM_MIN_MHZ 64
#define FM_MAX_MHZ 108
#define AM_MIN_KHZ 520
#define AM_MAX_KHZ 1710

#define RSSI_THRESHOLD 20 // dBµV
#define SNR_THRESHOLD 15  // dB
#define CW_VARIANCE_TH 300

static uint8_t calcVolume(uint16_t peak)
{
  uint8_t vol = map(peak, 50, 3000, 5, 55);
  return constrain(vol, 0, 63);
}
bool isCW(uint32_t v) { return v > CW_VARIANCE_TH; }

/* ─────── 初始化 ─────────────────────────────────────────────── */
void setup()
{
  Serial.begin(115200);
  pinMode(SPEAKER_MUTE, OUTPUT);
  digitalWrite(SPEAKER_MUTE, LOW);

  Wire.begin(21, 22, 400000);

  /* Si4732-A10：SEN 接 VDD → I²C 0x63 */
  radio.setDeviceI2CAddress(0); // 选择 0x11
  radio.setup(PIN_RST, 0);      // 默认 FM 模式启动
  // ----------- 新写法（10 kHz 单位）---------
  radio.setFM(FM_MIN_MHZ * 100,                 // 6400   → 64.00 MHz
              FM_MAX_MHZ * 100,                 // 10800  → 108.00 MHz
              10070,                            // 100.70 MHz
              10);                              // 20×10 kHz = 200 kHz 步进
  radio.setSeekFmSpacing(10);                   // 同理，搜台网格也改成 10
  radio.setSeekFmSNRThreshold(SNR_THRESHOLD);   // ✔ 正确命名 :contentReference[oaicite:2]{index=2}
  radio.setSeekFmRssiThreshold(RSSI_THRESHOLD); // ✔ 正确命名 :contentReference[oaicite:3]{index=3}

  radio.setVolume(30);
}

/* ─────── 主循环（轮询） ─────────────────────────────────────── */
void loop()
{
  static bool scanningFM = true;
  static uint32_t lastTuneMs = 0;

  if (millis() - lastTuneMs > 1000)
  {
    lastTuneMs = millis();

    /* 搜台：先 FM → 上限后切 AM */
    radio.seekStationUp();
    if (radio.getBandLimit())
    {
      scanningFM = !scanningFM;
      if (scanningFM)
      {
        radio.setFM(FM_MIN_MHZ * 100, FM_MAX_MHZ * 100,
                    FM_MIN_MHZ * 100, 10);
      }
      else
      {
        radio.setAM(AM_MIN_KHZ, AM_MAX_KHZ,
                    AM_MIN_KHZ, 9);
        radio.setSeekAmSpacing(9);
        radio.setSeekAmSNRThreshold(SNR_THRESHOLD);
        radio.setSeekAmRssiThreshold(RSSI_THRESHOLD);
      }
    }

    /* ---------- 信号质量 ---------- */
    uint8_t rssi = radio.getCurrentRSSI();
    uint8_t snr = radio.getCurrentSNR();

    /* ---------- 包络方差检测 CW ---------- */
    const uint8_t N = 40;
    uint32_t sum = 0, sumSq = 0;
    for (uint8_t i = 0; i < N; ++i)
    {
      uint16_t s = analogRead(AUDIO_PEAK_PIN);
      sum += s;
      sumSq += (uint32_t)s * s;
      delay(2);
    }
    uint32_t mean = sum / N;
    uint32_t variance = (sumSq / N) - (mean * mean);

    String mode;
    if (scanningFM)
      mode = "FM";
    else if (isCW(variance))
      mode = "CW";
    else
      mode = "AM";

    /* ---------- AGC ---------- */
    radio.setVolume(calcVolume(mean));

    /* ---------- 调试输出 ---------- */
    Serial.printf("%s  %7.2f kHz  RSSI=%u dBµ  SNR=%u dB  Vol=%u  Var=%lu\n",
                  mode.c_str(),
                  radio.getFrequency() / 10.0,
                  rssi, snr, radio.getVolume(), variance);
  }
}
