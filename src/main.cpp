#include <Arduino.h>
#include <ADF4351.h> // 内部已 #include <SPI.h>

/* ───────────────────────── ① 用户可改区 ───────────────────────── */
// --- 参考时钟 & 中频 ---
constexpr uint32_t REF_Hz = 50000000UL; // 板载 50 MHz 晶振（看你 PCB 标注 50.000 MHz）
constexpr uint32_t IF_Hz = 10700000UL;  // 中频 10.7 MHz（SA602）

// --- ESP32 ↔ ADF4351 引脚映射 ---
constexpr uint8_t ADF_LE = 5;    // SPI-CS / LE   （U8 图中 D5 → ADF4351 LE）
constexpr uint8_t ADF_SCK = 18;  // SPI-CLK       （D18 → CLK）
constexpr uint8_t ADF_MOSI = 23; // SPI-MOSI      （D23 → DATA）
constexpr int8_t ADF_MISO = -1;  // SPI-MISO不用
constexpr uint8_t ADF_CE = 32;   // PLL_CE        （D32 → CE）
constexpr uint8_t ADF_LD = 33;   // PLL_LD (可选) （D33 → LD）
// ───────────────────────────────────────────────────────────────── */

ADF4351 vfo(ADF_LE, SPI_MODE0, 1000000UL, MSBFIRST);//  1 MHz SPI，Mode0，MSB first

/* ========================= ② 只跑一次 ========================= */
void setup()
{
  Serial.begin(115200);

  // 初始化硬件 SPI：CLK / MISO / MOSI / SS
  SPI.begin(ADF_SCK, ADF_MISO, ADF_MOSI, ADF_LE);

  // 把 CE 拉高 —— 必须常高；库 **不会** 自动替你控制这根脚
  pinMode(ADF_CE, OUTPUT);
  digitalWrite(ADF_CE, HIGH);

  // 锁定 LD 状态检测（可选）
  pinMode(ADF_LD, INPUT_PULLUP);

  /* ------- ADF4351 上电初始化，只需做一次 ------- */
  vfo.setrf(REF_Hz); // (1) 告诉库：板上参考 = 50 MHz
  vfo.init();        // (2) 把 R5→R0 全写入
  vfo.enable();      // (3) 打开 RF 输出（仅写寄存器，不动 CE）
}

/* ================ ③ 持续循环，可实时改频 ================= */
void loop()
{
  /* —— ① 读取当前射频 —— */
  uint32_t rfHz = 100000000UL; // TODO: 换成你真实采集函数

  /* —— ② 计算本振 —— */
  uint32_t loHz = rfHz - IF_Hz; // 低变频：LO = RF – IF

  /* —— ③ 更新 ADF4351 —— */
  if (vfo.setf(loHz) == 0)
  {                           // setf() 仅重写必要寄存器
    if (!digitalRead(PIN_LD)) // LD 低＝锁定成功
      Serial.printf("RF=%u Hz  →  LO=%.3f MHz (Locked)\n", rfHz, loHz / 1e6);
    else
      Serial.println("PLL UNLOCK!");
  }
  else
  {
    Serial.println("setf() 计算失败——频率超范围或参数非法");
  }

  delay(10); // 根据系统需求调采样间隔；>100 µs PLL 就能锁
}

