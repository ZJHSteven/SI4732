#include "ADF4351_Controller.h"

/* ───────────────────── 内部常量与工具 ───────────────────── */
static constexpr uint32_t LO_MIN_Hz = 35000000UL;    // 35 MHz（芯片下限）
static constexpr uint64_t VCO_MIN_Hz = 2200000000ULL; // 2.2 GHz
static constexpr uint64_t VCO_MAX_Hz = 4400000000ULL; // 4.4 GHz
static constexpr uint16_t MOD = 4095;                // 12bit 分数模数

// 寄存器默认值（按你给的常用配置；R4 的 RF_DIV_SEL 会在运行时改）
static constexpr uint32_t R5_BASE = 0x00580005;
static constexpr uint32_t R4_BASE = 0x008C703C; // +5 dBm, RF_OUT_EN=1, MTLD=0
static constexpr uint32_t R3_BASE = 0x00C804B3; // Band Select clk div 等
static constexpr uint32_t R2_BASE = 0x0E008E42; // 负向极性、CP≈2.5mA、R计数器=1（配合 R_DIV=1）
static constexpr uint32_t R1_BASE = 0x80008001; // 仅作占位；实际会重写 MOD/PHASE
// R0 在运行时写 INT/FRAC

// 手动 SPI：上升沿采样，LE 包裹 32bit
static inline void adf_send32(uint32_t w)
{
  digitalWrite(ADF_LE, LOW);
  delayMicroseconds(1);
  for (int i = 31; i >= 0; --i)
  {
    digitalWrite(ADF_SCK, LOW);
    digitalWrite(ADF_MOSI, (w >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(ADF_SCK, HIGH);
    delayMicroseconds(1);
  }
  digitalWrite(ADF_LE, HIGH);
  delayMicroseconds(1);
  digitalWrite(ADF_LE, LOW);
}

// 一次性、惰性初始化引脚（首次调用 setRF 时执行）
static void adf_lazy_init()
{
  static bool inited = false;
  if (inited)
    return;
  pinMode(ADF_LE, OUTPUT);
  pinMode(ADF_SCK, OUTPUT);
  pinMode(ADF_MOSI, OUTPUT);
  pinMode(ADF_CE, OUTPUT);
  pinMode(ADF_LD, INPUT_PULLUP);

  digitalWrite(ADF_LE, LOW);
  digitalWrite(ADF_SCK, LOW);
  digitalWrite(ADF_MOSI, LOW);
  digitalWrite(ADF_CE, HIGH); // 使能芯片
  delay(5);

  // 上电初始化：R5→R0
  adf_send32(R5_BASE);
  adf_send32(R4_BASE);
  adf_send32(R3_BASE);
  adf_send32(R2_BASE);
  adf_send32(R1_BASE);
  adf_send32(0x00800000); // R0 占位
  delay(5);

  inited = true;
}

/* ─────────────────────── 单函数实现 ─────────────────────── */
uint32_t ADF4351_setRF(uint32_t rf_hz)
{
  adf_lazy_init();

  // 低变频：LO = RF - IF
  if (rf_hz <= IF_Hz)
  {
    Serial.println("[ADF4351] 参数错误：RF <= IF");
    return 0;
  }
  uint32_t lo = rf_hz - IF_Hz;
  if (lo < LO_MIN_Hz || (uint64_t)lo > VCO_MAX_Hz)
  {
    Serial.println("[ADF4351] LO 超出范围 (35 MHz ~ 4.4 GHz)");
    return 0;
  }

  // 将 LO 折算到 VCO 范围，并求 RF_DIV_SEL
  uint8_t rf_div_sel = 0;
  uint64_t vco = lo;
  while (vco < VCO_MIN_Hz && rf_div_sel < 6)
  {
    vco <<= 1; // ×2
    ++rf_div_sel;
  }
  if (vco > VCO_MAX_Hz)
  {
    Serial.println("[ADF4351] VCO 超限");
    return 0;
  }

  // 计算 INT/FRAC（四舍五入）
  const uint32_t PFD = (R_DIV == 0) ? 1u : (REF_Hz / R_DIV); // 保护
  if (PFD == 0)
  {
    Serial.println("[ADF4351] PFD=0（请检查 REF_Hz / R_DIV）");
    return 0;
  }
  uint32_t INT = static_cast<uint32_t>(vco / PFD);
  uint32_t rem = static_cast<uint32_t>(vco % PFD);
  uint32_t FRAC = (uint32_t)(((uint64_t)rem * MOD + (PFD / 2)) / PFD);

  // 边界检查（ADF4351 要求 INT ≥ 23）
  if (INT < 23 || INT > 65535 || FRAC > 4095)
  {
    Serial.println("[ADF4351] INT/FRAC 非法");
    return 0;
  }

  // 组帧寄存器
  uint32_t r5 = R5_BASE;
  uint32_t r4 = (R4_BASE & 0xFF8FFFFF) | ((uint32_t)(rf_div_sel & 0x7) << 20); // RF_DIV_SEL
  uint32_t r3 = R3_BASE;
  uint32_t r2 = R2_BASE;                                               // 注意：这里假定 R 计数器=1。如需使用 R_DIV≠1，请同步修改 R2_BASE。
  uint32_t r1 = (1UL << 15) | ((MOD & 0x0FFF) << 3) | 0x1;             // PHASE=1, MOD=4095, Addr=1
  uint32_t r0 = ((INT & 0xFFFF) << 15) | ((FRAC & 0x0FFF) << 3) | 0x0; // Addr=0

  // 写入 R5→R0
  adf_send32(r5);
  adf_send32(r4);
  adf_send32(r3);
  adf_send32(r2);
  adf_send32(r1);
  adf_send32(r0);
  delayMicroseconds(300);

  // 简单锁定检测（最多 ~5ms）
  bool locked = digitalRead(ADF_LD); // 立即读 LD
  delayMicroseconds(1000);           // 喂狗 / 给环路极短时间（可调 0~1000us）

  Serial.printf("[ADF4351] RF=%.3f MHz -> LO=%.3f MHz : %s\n",
                rf_hz / 1e6, lo / 1e6, locked ? "LOCK" : "UNLOCK");

  return locked ? lo : 0;
}
