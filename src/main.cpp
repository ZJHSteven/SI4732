#include <Arduino.h>
#include <SPI.h>

/* ───────────────────────── ① 用户可改区 ───────────────────────── */
// --- 参考时钟 & 中频 ---
constexpr uint32_t REF_Hz = 50000000UL; // 板载 50 MHz 晶振
constexpr uint32_t IF_Hz = 10700000UL;  // 中频 10.7 MHz（SA602）

// --- ESP32 ↔ ADF4351 引脚映射 ---
constexpr uint8_t ADF_LE = 5;    // SPI‑CS / LE
constexpr uint8_t ADF_SCK = 18;  // SPI‑CLK
constexpr uint8_t ADF_MOSI = 23; // SPI‑MOSI
constexpr int8_t ADF_MISO = -1;  // SPI‑MISO 不用
constexpr uint8_t ADF_CE = 32;   // PLL_CE
constexpr uint8_t ADF_LD = 33;   // PLL_LD (可选)

// --- ADF4351 PLL 参数 ---
constexpr uint16_t R_DIV = 1;                 // R 分频器
constexpr uint32_t PFD_FREQ = REF_Hz / R_DIV; // 鉴相频率 50 MHz
// ───────────────────────────────────────────────────────────────── */

/* ========================= ADF4351 手动控制类 ========================= */
class ADF4351_Manual
{
private:
  uint8_t le_pin;
  uint32_t reg[6]; // R5…R0（reg[5] == R5）

  // 写 32 bit 到 ADF4351（MSB first bit‑bang）
  void writeReg(uint32_t data)
  {
    digitalWrite(le_pin, LOW);
    delayMicroseconds(1);
    for (int i = 31; i >= 0; --i)
    {
      digitalWrite(ADF_SCK, LOW);
      digitalWrite(ADF_MOSI, (data >> i) & 1);
      delayMicroseconds(1);
      digitalWrite(ADF_SCK, HIGH);
      delayMicroseconds(1);
    }
    digitalWrite(le_pin, HIGH);
    delayMicroseconds(1);
    digitalWrite(le_pin, LOW);
  }

public:
  explicit ADF4351_Manual(uint8_t le) : le_pin(le)
  {
    /* 默认寄存器值 ‑ 依据数据手册 + 常用配置
       ‑ Digital Lock Detect
       ‑ 输出功率 +5 dBm / RF_OUT_EN=1 / MTLD=0
       ‑ 充电泵极性负向（环路常用，若板子做成正向请把 reg[2] 改 0x0E008E62）
    */
    reg[5] = 0x00580005; // R5
    reg[4] = 0x008C703C; // R4  (+5 dBm，RF_OUT_EN=1，MTLD=0)
    reg[3] = 0x00C804B3; // R3  BS_CLK_DIV = 400 (50 MHz / 125 kHz)
    reg[2] = 0x0E008E42; // R2  PD_POL=0, CP=2.5 mA
    reg[1] = 0x80008001; // R1  Phase=1, MOD 占位
    reg[0] = 0x00800000; // R0  INT/FRAC 占位
  }

  void init()
  {
    pinMode(le_pin, OUTPUT);
    pinMode(ADF_SCK, OUTPUT);
    pinMode(ADF_MOSI, OUTPUT);
    digitalWrite(le_pin, LOW);
    delay(10);
    // R5→R0 顺序写入
    for (int i = 5; i >= 0; --i)
    {
      writeReg(reg[i]);
      delayMicroseconds(200);
    }
    delay(10);
    Serial.println("ADF4351 寄存器初始化完成");
  }

  /* ---------------- 设定输出频率 (Hz) ---------------- */
  bool setFrequency(uint32_t fout)
  {
    if (fout < 35000000UL || fout > 4400000000UL)
    {
      Serial.printf("频率 %.3f MHz 超出 35 MHz–4.4 GHz\n", fout / 1e6);
      return false;
    }

    /* 计算 VCO 频率 & RF_DIV_SEL */
    uint8_t rf_div_sel = 0;
    uint32_t vco_freq = fout;
    while (vco_freq < 2200000000UL && rf_div_sel < 6)
    {
      vco_freq <<= 1; // ×2
      ++rf_div_sel;
    }
    if (vco_freq > 4400000000UL)
    {
      Serial.println("VCO 频率超限");
      return false;
    }

    /* 计算 INT / FRAC / MOD */
    const uint16_t MOD = 4095;
    uint16_t INT = vco_freq / PFD_FREQ;
    uint32_t remainder = vco_freq % PFD_FREQ;
    uint16_t FRAC = (remainder == 0) ? 0 : (uint16_t)(((uint64_t)remainder * MOD) / PFD_FREQ); // 64 bit 乘法防溢出

    if (INT < 23 || INT > 65535)
    {
      Serial.println("INT 超范围");
      return false;
    }

    /* 构建寄存器 */
    reg[0] = ((uint32_t)INT << 15) | ((uint32_t)FRAC << 3);
    reg[1] = (1UL << 15) | ((uint32_t)MOD << 3) | 0x1;
    reg[4] = (reg[4] & 0xFF8FFFFF) | ((uint32_t)rf_div_sel << 20);

    /* 写入 R5→R0 */
    writeReg(reg[5]);
    writeReg(reg[4]);
    writeReg(reg[3]);
    writeReg(reg[2]);
    writeReg(reg[1]);
    writeReg(reg[0]);
    delayMicroseconds(300);

    Serial.printf("PLL参数: VCO=%.3f MHz, INT=%u, FRAC=%u, MOD=%u, RF_DIV=%u\n",
                  vco_freq / 1e6, INT, FRAC, MOD, 1u << rf_div_sel);
    return true;
  }

  /* ---------------- 输出使能 ---------------- */
  void enable()
  {
    writeReg(reg[4]); // R4 里已包含 RF_OUT_EN=1 / +5 dBm / MTLD=0
  }

  /* ---------------- 锁定检测 ---------------- */
  bool isLocked()
  { // Digital LD: 高电平 = 锁定
    return digitalRead(ADF_LD);
  }

  /* 可选：打印状态，用于调试 */
  void printStatus(uint32_t target_freq)
  {
    Serial.printf("=== ADF4351 调试信息 ===\n");
    Serial.printf("目标频率: %.3f MHz\n", target_freq / 1e6);
    Serial.printf("参考频率: %.3f MHz\n", REF_Hz / 1e6);
    Serial.printf("鉴相频率: %.3f MHz\n", PFD_FREQ / 1e6);
    Serial.printf("锁定状态: %s (LD=%d)\n", isLocked() ? "已锁定" : "未锁定", digitalRead(ADF_LD));
    uint16_t INT = (reg[0] >> 15) & 0xFFFF;
    uint16_t FRAC = (reg[0] >> 3) & 0xFFF;
    uint16_t MOD = (reg[1] >> 3) & 0xFFF;
    uint8_t rf_d = (reg[4] >> 20) & 0x7;
    Serial.printf("PLL参数: INT=%u, FRAC=%u, MOD=%u, RF_DIV=2^%u\n", INT, FRAC, MOD, rf_d);
    uint64_t vco = (uint64_t)INT * PFD_FREQ + ((uint64_t)FRAC * PFD_FREQ) / MOD;
    Serial.printf("VCO=%.6f MHz, 输出=%.6f MHz\n", vco / 1e6, (double)vco / (1UL << rf_d) / 1e6);
    for (int i = 5; i >= 0; --i)
      Serial.printf("R%d: 0x%08X\n", i, reg[i]);
    Serial.println("========================");
  }
};

ADF4351_Manual vfo(ADF_LE);

/* ========================= ② 只跑一次 ========================= */
void setup()
{
  Serial.begin(115200);
  pinMode(ADF_CE, OUTPUT);
  digitalWrite(ADF_CE, HIGH); // 使能芯片
  pinMode(ADF_LD, INPUT_PULLUP);

  vfo.init();
  vfo.enable();

  Serial.println("ADF4351 初始化完成");
  Serial.printf("参考频率: %.1f MHz\n", REF_Hz / 1e6);
  Serial.printf("中频: %.1f MHz\n", IF_Hz / 1e6);
}

/* ================ ③ 持续循环，可实时改频 ================= */
void loop()
{
  uint32_t rfHz = 108000000UL;  // TODO: 换成实时射频读数
  uint32_t loHz = rfHz - IF_Hz; // 低变频：LO = RF – IF
  if (loHz < 35000000UL)
    loHz = 35000000UL;

  if (vfo.setFrequency(loHz))
  {
    // 等待锁定，最多 100 ms
    for (int i = 0; i < 100; ++i)
    {
      if (vfo.isLocked())
      {
        Serial.printf("✓ RF=%.1f MHz → LO=%.3f MHz (已锁定, %d ms)\n", rfHz / 1e6, loHz / 1e6, i + 1);
        break;
      }
      delay(1);
    }
  }
  else
  {
    Serial.printf("✗ LO=%.3f MHz 设定失败\n", loHz / 1e6);
  }
  delay(1000);
}
