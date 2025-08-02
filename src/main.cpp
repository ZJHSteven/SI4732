#include "ADF4351_Controller.h"
#include "SI4732_Scanner.h"

#define RF_SW_PIN 25
#define RF_SW_FM_ACTIVE_HIGH 1 // 若你的硬件“低电平=FM”，把 1 改 0

static inline void RF_select_path(bool fm)
{
  pinMode(RF_SW_PIN, OUTPUT);
#if RF_SW_FM_ACTIVE_HIGH
  digitalWrite(RF_SW_PIN, fm ? HIGH : LOW);
#else
  digitalWrite(RF_SW_PIN, fm ? LOW : HIGH);
#endif
}

// 判别 AM/Fm：在中心与边沿各测一次 SNR
static bool classify_fm_like(uint32_t rf_hz, int16_t &snr_center, int16_t &snr_slope)
{
  const uint32_t DF = 50000UL; // 50 kHz，可在 30~60 kHz 之间调
  int16_t rssi_dummy;

  // 中心
  ADF4351_setRF(rf_hz);
  delayMicroseconds(200);
  SI_getQuality(rssi_dummy, snr_center);

  // 滤波器边沿（IF = 10.7MHz + DF ⇒ RF 改为 rf_hz - DF）
  ADF4351_setRF(rf_hz - DF);
  delayMicroseconds(200);
  SI_getQuality(rssi_dummy, snr_slope);

  // 复原中心（可选）
  ADF4351_setRF(rf_hz);
  return (snr_slope - snr_center) >= 6; // 阈值可调：4~8 dB
}

// 可选：打开两次采样取最大，提升可靠性（极短）
#define DOUBLE_SAMPLE 1

void sweep_88_108_print_every_step()
{
  const uint32_t START = 88000000UL; // 88 MHz
  const uint32_t STOP = 108000000UL; // 108 MHz
  const uint32_t STEP = 100000UL;    // 100 kHz
  const uint32_t TOTAL = ((STOP - START) / STEP) + 1;

  if (!SI_init_AM_IF107())
  {
    Serial.println("[SI4732] 初始化失败");
    return;
  }

  int16_t best_rssi = INT16_MIN;
  int16_t best_snr = INT16_MIN;
  uint32_t best_rf = 0;

  uint32_t ok = 0, fail = 0;
  uint32_t t0 = micros();

  // —— 逐步扫频（升序；如你要降序，改 for 初值与方向即可）——
  uint32_t idx = 0;
  for (uint32_t rf = START; rf <= STOP; rf += STEP, ++idx)
  {

    // 1) 设 LO（按你的函数：低变频 LO=RF-IF；内部已不等待）
    (void)ADF4351_setRF(rf);

    // 2) 极短 settle（建议 100~300us；你要更快可减到 0~50us）
    delayMicroseconds(200);

    // 3) 读质量（每步都打印）
    int16_t rssi = -32768, snr = -32768;
#if DOUBLE_SAMPLE
    int16_t r1, s1, r2, s2;
    if (SI_getQuality(r1, s1))
    {
      ++ok;
      // 立刻复读一次，取最大（花费 ~几十微秒）
      if (SI_getQuality(r2, s2))
      {
        if (r2 > r1)
        {
          r1 = r2;
          s1 = s2;
        }
      }
      rssi = r1;
      snr = s1;
    }
    else
    {
      ++fail;
    }
#else
    if (SI_getQuality(rssi, snr))
      ++ok;
    else
      ++fail;
#endif

    const uint32_t lo = (rf > IF_Hz) ? (rf - IF_Hz) : 0;

    // —— 每步串口打印（你要“每一步都打印”，就在这里）——
    // STEP, RF, LO, RSSI, SNR
    Serial.printf("[STEP %4lu/%lu] RF=%.6f MHz  LO=%.6f MHz  RSSI=%d dBµV  SNR=%d dB\n",
                  (unsigned long)(idx + 1), (unsigned long)TOTAL,
                  rf / 1e6, lo / 1e6, rssi, snr);

    // 4) 维护 Top1（先比 RSSI，若相同比 SNR）
    if (rssi != -32768)
    {
      bool better = (rssi > best_rssi) || (rssi == best_rssi && snr > best_snr);
      if (better)
      {
        best_rssi = rssi;
        best_snr = snr;
        best_rf = rf;
      }
    }
  }

  uint32_t dt_us = micros() - t0;

  Serial.printf("[SCAN] 88~108MHz@100kHz steps=%lu ok=%lu fail=%lu time=%.3f ms avg=%.3f ms/step\n",
                (unsigned long)TOTAL, (unsigned long)ok, (unsigned long)fail,
                dt_us / 1000.0, (dt_us / 1000.0) / TOTAL);

  if (best_rf)
  {
    const uint32_t best_lo = best_rf - IF_Hz;
    Serial.printf("[RESULT] Top1 RF=%.6f MHz  (LO=%.6f MHz)  RSSI=%d dBµV  SNR=%d dB\n",
                  best_rf / 1e6, best_lo / 1e6, best_rssi, best_snr);

    // —— 新增：斜坡判别 + 自动切换 —— //
    int16_t snr_c = 0, snr_s = 0;
    bool fm_like = classify_fm_like(best_rf, snr_c, snr_s);
    Serial.printf("[CLASSIFY] SNR_center=%d dB  SNR_slope=%d dB  => %s\n",
                  snr_c, snr_s, fm_like ? "FM-like" : "AM-like");

    if (fm_like)
    {
      // 选 FM 路 + 切 FM 模式并直接调到 RF
      RF_select_path(true);
      if (SI_set_mode_FM(best_rf))
      {
        delay(5); // 给 FM 前端/AGC 一点时间，可酌情减小
        int16_t rssi2, snr2;
        SI_getQuality(rssi2, snr2);
        Serial.printf("[SWITCH->FM] RF=%.6f MHz  RSSI=%d dBµV  SNR=%d dB\n",
                      best_rf / 1e6, rssi2, snr2);
      }
      else
      {
        Serial.println("[SWITCH->FM] 切换失败（SI未就绪？）");
      }
    }
    else
    {
      // 选 AM 路 + 回 AM@10.7MHz 监听
      RF_select_path(false);
      SI_set_mode_AM_IF107();
      Serial.println("[SWITCH->AM] IF=10.7 MHz 持续侦听");
    }

    // 扫完把 LO 停在最强台，避免“几秒后看不到”
    ADF4351_setRF(best_rf);
  }
  else
  {
    Serial.println("[RESULT] 未找到有效峰值");
  }
}
void setup()
{
  Serial.begin(115200);
  while (!Serial) { /* 等待串口连接 */ }

  Serial.println("[INFO] 开始扫频 88~108MHz@100kHz");
  sweep_88_108_print_every_step();
}

void loop()
{
  // 主循环为空，因为扫频在 setup() 中已完成
  // 如果需要重复扫频，可以在这里添加代码
  delay(1000); // 避免空循环占用过多 CPU
}