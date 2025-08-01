/***********************************************************************
 *  Si4732 自动搜台 + AM／FM／CW 判别 + 音量/AGC 闭环  (ESP32)
 *  -- 2025 电赛 FT 自动调谐接收机演示
 *  硬件与连线见“上下文提示词”表格
 **********************************************************************/
#include <Wire.h>
#include <SI4735.h>

SI4735 radio;

/* ─────── 用户可调常量 ─────────────────────────────────────────── */
#define PIN_RST 27
#define PIN_INT 25        // 若未接中断，可注释掉 INT 相关代码
#define AUDIO_PEAK_PIN 36 // ADC1_CH0  (VP)
#define SPEAKER_MUTE 4    // PAM8403 SHDN

#define FM_MIN_MHZ 64
#define FM_MAX_MHZ 108
#define AM_MIN_KHZ 520
#define AM_MAX_KHZ 1710

#define RSSI_THRESHOLD 20  // dBμV，低于此视为无台
#define SNR_THRESHOLD 15   // dB
#define CW_VARIANCE_TH 300 // 音频包络方差阈值，区分 AM 语音 vs. CW

/* AGC: 读音频峰值，把 0–3.3 V → 0-4095 映射到库 0-63 音量 */
static uint8_t calcVolume(uint16_t peak)
{
  // 根据实测线性或对数映射，自行微调
  uint8_t vol = map(peak, 50, 3000, 5, 55);
  vol = constrain(vol, 0, 63);
  return vol;
}

/* 快速检测 CW（简单包络方差法，可替换为更复杂 DSP） */
bool isCW(uint32_t variance)
{
  return variance > CW_VARIANCE_TH;
}

/* 读取 RSSI/SNR（库 2.1.x 建议先调用 getCurrentReceivedSignalQuality()） */
void readSignalQuality(uint8_t &rssi, uint8_t &snr)
{
  radio.getCurrentReceivedSignalQuality(1);          // 清 RSQINT
  rssi = radio.getReceivedSignalStrengthIndicator(); // dBμV
  snr = radio.getStatusSNR();                        // dB
}

/* ─────── 初始化 ─────────────────────────────────────────────── */
void setup()
{
  Serial.begin(115200);
  pinMode(SPEAKER_MUTE, OUTPUT);
  digitalWrite(SPEAKER_MUTE, LOW);

  Wire.begin(21, 22, 400000); // SDA, SCL, 400 kHz
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(50);

  // 如果用 INT，加上这句；否则省略
  pinMode(PIN_INT, INPUT_PULLUP);

  // 建议让库自己探测 I²C 地址：radio.getDeviceI2CAddress()
  radio.setup(0x63); // SEN 接 VDD 固定 0x63（库 2.1.x 兼容） :contentReference[oaicite:0]{index=0}

  /* 默认先进 FM，64-108 MHz，初始 100.7 MHz，步进 200 kHz */
  radio.setFM(FM_MIN_MHZ * 1000, FM_MAX_MHZ * 1000, 100700, 200); // :contentReference[oaicite:1]{index=1}
  radio.setSeekFmSpacing(200);                                    // 200 kHz 网格
  radio.setSeekFmSNRThreshold(SNR_THRESHOLD);
  radio.setSeekFmRssiThreshold(RSSI_THRESHOLD);

  /* 若接 INT，可打开 STC/RSQ 中断提高响应 */
  // radio.setGpioIen(1, 1, 0, 0, 0, 0);  // STCIEN=1, RSQIEN=1 :contentReference[oaicite:2]{index=2}

  radio.setVolume(30); // 预设中等音量
}

/* ─────── 主循环 ─────────────────────────────────────────────── */
void loop()
{
  static bool scanningFM = true; // 状态机：先扫 FM，再扫 AM
  static uint32_t lastTuneMs = 0;

  /* 每秒执行一次搜台（手动轮询，若接 INT 可改用中断回调） */
  if (millis() - lastTuneMs > 1000)
  {
    lastTuneMs = millis();

    /* ── 搜台 ───────────────────────── */
    if (scanningFM)
    {
      radio.seekStationUp();    // 向上搜 FM
      if (radio.getBandLimit()) // 到上限，切 AM
      {
        scanningFM = false;
        radio.setAM(AM_MIN_KHZ, AM_MAX_KHZ, AM_MIN_KHZ, 9); // 9 kHz 步进
        radio.setSeekAmSpacing(9);
      }
    }
    else
    {
      radio.seekStationUp();    // 向上搜 AM
      if (radio.getBandLimit()) // 到上限，回 FM
      {
        scanningFM = true;
        radio.setFM(FM_MIN_MHZ * 1000, FM_MAX_MHZ * 1000, FM_MIN_MHZ * 1000, 200);
      }
    }

    /* ── 判别调制 ───────────────────── */
    uint8_t rssi, snr;
    readSignalQuality(rssi, snr);

    // ADC 连续取 N 点计算方差，用于区分 CW
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

    /* ── AGC/音量闭环 ───────────────── */
    uint8_t volume = calcVolume(mean);
    radio.setVolume(volume);

    /* ── 调试输出 ───────────────────── */
    Serial.printf("%s  %7.2f kHz  RSSI=%u dBµ  SNR=%u dB  Vol=%u  Var=%lu\n",
                  mode.c_str(),
                  radio.getFrequency() / 10.0,
                  rssi, snr, volume, variance);
  }
}
