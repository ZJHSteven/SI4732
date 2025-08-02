#include "ADF4351_Controller.h"
#include "SI4732_Scanner.h"

// 88~108 MHz，100 kHz步进扫频并计时
void sweep_find_best_station()
{
  const uint32_t START = 88000000UL;                  // 88 MHz
  const uint32_t STOP = 108000000UL;                  // 108 MHz
  const uint32_t STEP = 100000UL;                     // 100 kHz
  const uint32_t TOTAL = ((STOP - START) / STEP) + 1; // 步数（含端点）

  // 初始化 SI4732（AM @ 10.7MHz）
  if (!SI_init_AM_IF107())
  {
    Serial.println("[SI4732] 初始化失败");
    return;
  }

  int16_t best_rssi = INT16_MIN;
  int16_t best_snr = INT16_MIN;
  uint32_t best_rf = 0;
  uint32_t ok = 0, fail = 0;

  // 计时（用 micros 精度更高；最后转 ms 打印）
  uint32_t t0 = micros();
  for (uint32_t rf = START; rf <= STOP; rf += STEP)
  {
    // 1) 设 LO （不等待）
    (void)ADF4351_setRF(rf);

    // 2) 极短 settle（让混频/10.7滤波器+SI4732 检波稳定一点点）
    // 你要“尽量不等”，这里给 200us，按需改 0~500us
    delayMicroseconds(200);

    // 3) 读 IF 质量
    int16_t rssi, snr;
    if (SI_getQuality(rssi, snr))
    {
      ++ok;
      // 选 Top1：先比 RSSI，若相同比 SNR
      bool better = (rssi > best_rssi) || (rssi == best_rssi && snr > best_snr);
      if (better)
      {
        best_rssi = rssi;
        best_snr = snr;
        best_rf = rf;
      }
    }
    else
    {
      ++fail;
    }
  }

  uint32_t dt_us = micros() - t0;

  Serial.printf("[SCAN] 88~208MHz@100kHz steps=%lu ok=%lu fail=%lu time=%.3f ms avg=%.3f ms/step\n",
                (unsigned long)TOTAL, (unsigned long)ok, (unsigned long)fail,
                dt_us / 1000.0, (dt_us / 1000.0) / TOTAL);

  if (best_rf)
  {
    // 计算对应 LO（低变频）
    uint32_t lo = (best_rf > IF_Hz) ? (best_rf - IF_Hz) : 0;
    Serial.printf("[RESULT] Top1 RF=%.6f MHz  (LO=%.6f MHz)  RSSI=%d dBµV  SNR=%d dB\n",
                  best_rf / 1e6, lo / 1e6, best_rssi, best_snr);
  }
  else
  {
    Serial.println("[RESULT] 未找到有效峰值（请检查链路/步进/门限）");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  uint32_t rf = 108000000UL; // 108 MHz
  uint32_t lo = ADF4351_setRF(rf);
  // lo==0 表示失败；非 0 即当前 LO 频率（Hz）
  sweep_find_best_station();
}

void loop()
{
  // 后续你可在这里做扫频等逻辑，只需重复调用 ADF4351_setRF(...)
}
