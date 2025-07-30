你这轮的问题可以分成 6 块：**（A）RSSI / Vpp / SNR 的区别与判别逻辑**、**（B）“FM 下扫台能扫到 AM/CW”的原因**、**（C）AM 解调的几条实现路线（从最简到更稳）**、**（D）是否需要额外 LNA、信号如何走线**、**（E）有没有“一颗芯片式”的 AM 解调模块**、**（F）AGC 到 1 V ±0.1 V 的可行性**。我把每块讲清，并给出**可落地的连接图**。

---

## A. RSSI / Vpp / SNR 分别是什么？怎么用来判 FM / AM / CW？

* **RSSI**：*射频*强度指示（功率），看的是**载波强不强**，跟是不是有调制、调什么制式**无关**。所以 AM/FM/CW 只要同频、场强大，RSSI 都会高。([维基百科][1])
* **Vpp（包络/音频幅度）**：这是**解调后**在音频端看到的峰峰值。不同解调器得到的 Vpp 代表的是**调制**信息（AM 的包络、FM 的基带）。
* **SNR**：音频信噪比。Si47xx 在 FM 模式能直接回报 SNR / VALID 等状态字节，用来判断“这个频点 FM 解调是不是像样”。([skyworksinc.com][2])

> 关键点：**RSSI≠Vpp**。RSSI 在“射频端”，Vpp 在“音频端”。

**一键判别的实用流程（建议阈值，赛前用 tinySA 标定一遍）：**

1. **FM 方式扫台**（100 kHz 步进），拿到若干 RSSI 高的候选频点。
2. 对每个候选频点：

   * 2.1 **在 FM 模式**读取 SNR 与音频 Vpp：

     * SNR ≥ 25 dB 且 Vpp 高 ⇒ **FM**（成立就直接输出 FM 音频）。
   * 2.2 **切到 AM 解调链**（外接包络或同步检波），测 **Vpp\_AM**：

     * SNR\_FM 低、而 Vpp\_AM 中‑高 ⇒ **AM**。
     * SNR\_FM 低、Vpp\_AM ≈ 0 ⇒ **CW（或几乎未调制）**。
3. 选定体制后，进入 **AGC 闭环**把输出锁到 1 V ±0.1 Vpp。

---

## B. “FM 扫台会把 AM/CW 也扫进来”的意思

Si47xx 的 **seek 判定首先依赖 RSSI/VALID**（“这有个强载波/像频道”），而不是“它是 FM/AM 吗”。因此**强 AM 或 CW** 在该频点也会被认为“有台”，进入候选集合；但一旦你在该点**尝试 FM 解调**，就会发现 **SNR\_FM 很低、音频失真/能量低**，这时再切到 AM 检波看 Vpp\_AM，就能把它们分出来。这就是上面流程第 2 步的意义。Si47xx 的编程指南里也专门说，命令会回**RSSI、SNR、VALID**等状态做质量判断。([skyworksinc.com][2])

---

## C. “RC 包络能不能用？有没有更稳的做法？”

**C‑1. 最简：二极管 + RC（包络检波）**

* 原理：检波二极管把 88–108 MHz 的高频整流，RC 低通留下 300 Hz–3.4 kHz 包络。
* 选型&参数：**BAT54（肖特基）** 或 1N4148；R≈4.7 kΩ，C≈22–33 nF（f\_{‑3 dB}≈1 kHz），就能覆盖语音带宽。做法正确时可以满足“无明显失真”的竞赛要求。([ee-diary][3])
* 注意：检波输出阻抗较高，后面要**缓冲**（见 C‑3）。

**C‑2. 更稳的“集成包络”**

* 用**专用 RF 包络/对数检波器**，对 –85…–60 dBm 的小信号更友好、带宽余量大、对飞线更不敏感：

  * **LT5534**（50 MHz–3 GHz，60 dB 动态范围），直接出 DC 包络，后面再 AC 耦合成音频。([模拟芯片官网][4])
  * **ADL5513**（1 MHz–4 GHz，对数检波，响应快），也常见于现成模块。([模拟芯片官网][5])
* 这些芯片在国内电商/现货模块上都**较好买**（常见“LT5534 检波模块”“ADL5513 功率检波模块”关键词）。

**C‑3. 缓冲与切换（为什么提 74HC4053 / 射随）**

* **缓冲**：无论二极管还是检波芯片，输出都不适合直接拖 8 Ω 负载；加一个**射随或单电源运放**把阻抗降下来，失真会明显小。
* **切换**：用 **74HC4053** 把“FM 解调音频（Si47xx）/AM 检波音频/静音”三路切到同一后级功放，便于 MCU 全自动体制切换。

**C‑4. 同步/乘法检波（更“教科书式”的 AM）**

* 用 **SA612/NE602**（500 MHz 信号、200 MHz 本振可用），把载波与本振相乘得到基带（相当于同步检波）。需要给它一个 88–108 MHz 的本振；本振偏差会带来失真，但对语音宽带要求不高时可接受。([NXP半导体][6], [AllDatasheet][7])
* 优点：对弱 AM（接近 –85 dBm）更稳；缺点：要加 LO（VCO/小晶振/锁相），硬件复杂度高于 C‑2。

> **推荐取舍**：若时间紧、要抗飞线与失配风险，**优先 C‑2（LT5534/ADL5513 模块）**；其余保持简洁：缓冲 + 4053 切换即可。

---

## D. 要不要再加 LNA？信号怎么走？

**竞赛输入范围 –85…–60 dBm**。Si4732 的 FM 前端自带 LNA/AGC，**FM 路不需要再加 LNA**。AM 路若用 **二极管 RC**，在 –85 dBm 处确实偏勉强；若改用 **LT5534/ADL5513**，就**不必额外 LNA**。真的担心裕量，可以加**可旁路的前置 LNA（+15 dB）**，但要考虑在 –60 dBm 端不会把 FM 前端推饱和（用继电器/开关在 AM 需要时才插入）。

**推荐走线/方框图：**

```
SMA(50Ω) ── 50Ω分路 ┬── 路A：Si4732（FM解调）──► Audio_A
                     │
                     └── 路B：AM检波(二选一)
                           [简易]二极管+RC+缓冲
                           [稳妥]LT5534/ADL5513模块+缓冲 ─► Audio_B
Audio_A / Audio_B  ── 74HC4053(受MCU控制) ──► PAM8403(8Ω) ─► 1 Vpp 目标
```

> **是否“经过”Si4732？**
> AM 体制时**不走 Si4732 的解调**，而是**RF 在分路处直接进 AM 检波器**；Si4732 仍用于扫台/读 RSSI/SNR 与体制判定。

---

## E. 有没有“一颗芯片式”的 AM 解调模块（国内好买）？

有两类，**都常见有成品小板**：

* **RF 包络检波模块**：基于 **LT5534**（50 MHz–3 GHz）或 **ADL5513**（1 MHz–4 GHz），直出包络（后面简单去直流即可得到音频）。指标、带宽、动态范围都远胜“裸二极管 RC”。([模拟芯片官网][4], [模拟芯片官网][5])
* **同步检波/乘法检波模块**：基于 **SA612/NE602**。你还需要一个 88–108 MHz 的本振（可以用小 VCO/压控+分频），整体更复杂。([NXP半导体][6], [AllDatasheet][7])

**考虑赛程与搭棚风险**：优先买 **LT5534（或 ADL5513）小模块**做 AM 路，一根同轴把 RF 分出来接过去，几乎“即插即用”。

---

## F. 1 V ±0.1 Vpp 的 AGC 还坚持“只靠 63 级音量”吗？

你担心得对：**1 dB/级** 的数字音量，电压步进 ≈ **+12 %**，确实**比 ±10 % 的目标还“粗”**。但这不是死局：

* 方案 1（硬件细调）：在 Si4732 后加 **数字电位器/可编程衰减器**（如 256 级），做“**粗调（1 dB）+ 细调（0.4 dB）**”。
* 方案 2（纯软件抖动）：在 50 ms 控制周期里，在 **N 与 N+1 两档间按占空比切换**，等效得到“亚 1 dB”的平均增益（常用于音频/电源数字控制，听感无拍频）。
* 无论哪种，PAM8403 的**固定电压增益 24 dB**是常规值，足够；它本身不提供细颗粒度增益，调节仍放在前级实现。([DigiKey][8], [AllDatasheet][9])

---

## 最后，再回答两句你提到的“芯片到底能不能 AM”的根因

**Si4732 的频段：FM 64–108 MHz；AM 520–1710 kHz（另有 LW/SW）**。也就是说，**它的 AM 解调并不覆盖 88–108 MHz**，所以我们才需要 AM 外挂检波链路。这在多个官方/渠道版 datasheet 上都写得很清楚。([pdf.ic37.com][10], [Mouser][11])

---

### 你可以直接照着干的最小改动版方案

* **FM 路**：保持 Si4732（扫台、解调、读 SNR）。
* **AM 路**：买 **LT5534（或 ADL5513）模块** → 模块输出经缓冲 → 进 74HC4053。
* **分路**：SMA 后用 50 Ω 电阻三分器/微型功分器分成 Si4732 和 AM 模块两路。
* **体制判别**：按“FM‑SNR 高即 FM；否则看 AM‑Vpp；两者皆低即 CW”那套阈值法。
* **AGC**：保留 Si4732 63 级作“粗调”，再加“细调”（数字电位器或抖动法），就能稳到 **1 V ±0.1 Vpp**。

需要的话，我可以把**阈值判别伪代码 + 4053 控制时序 + AGC 抖动实现**写成 80 行以内的示例给你。届时也可以把你 tinySA 的幅度‑阈值标定脚本一起补上。

[1]: https://en.wikipedia.org/wiki/Received_signal_strength_indicator?utm_source=chatgpt.com "Received signal strength indicator"
[2]: https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf?utm_source=chatgpt.com "AN332: Si47xx Programming Guide - skyworksinc.com"
[3]: https://www.ee-diary.com/2023/01/Designing-Envelope-Detector-Circuit-AM-Demodulation.html?utm_source=chatgpt.com "Designing an Envelope Detector Circuit for AM Demodulation"
[4]: https://www.analog.com/media/en/technical-documentation/data-sheets/5534fc.pdf?utm_source=chatgpt.com "LT5534 - 50MHz to 3GHz RF Power Detector with 60dB Dynamic Range - Analog"
[5]: https://www.analog.com/cn/products/adl5513.html?utm_source=chatgpt.com "ADL5513 | datasheet and product info 1 MH<font style=\"text ..."
[6]: https://www.nxp.com/docs/en/data-sheet/SA612A.pdf?utm_source=chatgpt.com "SA612A Double-balanced mixer and oscillator - NXP Semiconductors"
[7]: https://www.alldatasheet.com/datasheet-pdf/pdf/18897/PHILIPS/SA612.html?utm_source=chatgpt.com "SA612 Datasheet (PDF) - NXP Semiconductors"
[8]: https://www.digikey.com/htmldatasheets/production/1282043/0/0/1/pam8403.html?utm_source=chatgpt.com "PAM8403 Datasheet by Diodes Incorporated - Digi-Key Electronics"
[9]: https://www.alldatasheet.com/datasheet-pdf/pdf/246505/PAM/PAM8403.html?utm_source=chatgpt.com "PAM8403 Datasheet (PDF) - Power Analog Micoelectronics"
[10]: https://pdf.ic37.com/SILABS/SI4732-A10-GSR_datasheet_20065541/?utm_source=chatgpt.com "SI4732-A10-GSR (SILICON) PDF技术资料下载 SI4732-A10 ..."
[11]: https://www.mouser.com/datasheet/2/472/Si4732_A10_short-2492991.pdf?utm_source=chatgpt.com "Si4732-A10 Broadcast AM/FM/SW/LW/RDS Radio Receiver - Mouser Electronics"
