#include <Wire.h>
#include <SI4735.h>
#define RST_PIN 27
SI4735 radio;

struct Hit
{
  uint16_t f10k;
  uint8_t rssi;
  uint8_t snr;
};

static inline void readRSQ(uint8_t &rssi, uint8_t &snr)
{
  radio.getCurrentReceivedSignalQuality();
  rssi = radio.getCurrentRSSI();
  snr = radio.getCurrentSNR();
}

// 将一个候选写入Top-N（按SNR优先，RSSI次之）
static void pushTopN(Hit hit, Hit top[], int N)
{
  for (int i = 0; i < N; ++i)
  {
    if (hit.snr > top[i].snr || (hit.snr == top[i].snr && hit.rssi > top[i].rssi))
    {
      for (int j = N - 1; j > i; --j)
        top[j] = top[j - 1];
      top[i] = hit;
      break;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(21, 22, 800000);

  if (radio.getDeviceI2CAddress(RST_PIN) == 0)
  {
    Serial.println("Si47xx 未响应。");
    while (1)
      delay(1000);
  }
  radio.setup(RST_PIN, 0);
  radio.setFM(6400, 10800, 8800, 10); // 波段参数；步进只影响seek，不影响手动扫
  radio.setAudioMode(SI473X_ANALOG_AUDIO);
  radio.setVolume(40);
}

void loop()
{
  const uint16_t START_10K = 8800, STOP_10K = 10800; // 88.00~108.00 MHz
  const uint16_t STEP_10K = 10;                      // 100 kHz
  const uint16_t SETTLE_MS = 1;                     // 每步稳定时间 8~12ms

  Hit top[5] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  uint32_t t0 = millis();

  // —— 快扫：不打印，只保留Top-5 —— //
  for (uint16_t f10k = START_10K; f10k <= STOP_10K; f10k += STEP_10K)
  {
    radio.setFrequency(f10k);
    delay(SETTLE_MS); // 关键：短暂停就够
    uint8_t rssi, snr;
    readRSQ(rssi, snr);
    pushTopN({f10k, rssi, snr}, top, 5);
  }

  uint32_t elapsed = millis() - t0;

  // —— 一次性输出 —— //
  Serial.printf("SWEEP DONE: %lu ms\n", (unsigned long)elapsed);
  for (int i = 0; i < 5; ++i)
  {
    if (top[i].f10k)
      Serial.printf("#%d  %6.2f MHz  RSSI=%u dBµV  SNR=%u dB\n",
                    i + 1, top[i].f10k / 100.0, top[i].rssi, top[i].snr);
  }

  // 如果要连续循环扫，延时一下再来；比赛只需一次就改成 while(1)。
  delay(500);
}
