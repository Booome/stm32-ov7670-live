# OV7670 SCCB 驱动设计

> 模块：`ov7670_sccb.c/h` | 日期：2026-07-09 | 依赖：`dwt_delay`

---

## 1. 模块边界

SCCB 模块是纯传输层，负责 GPIO bit-bang 的 SCCB/I2C 读写时序，不包含任何 OV7670 寄存器知识。

| 职责 | 不负责 |
|------|--------|
| START/STOP/RESTART 信号生成 | OV7670 寄存器配置表 |
| 字节级读写 + ACK 检查 | 寄存器含义和值 |
| SCL/SDA 引脚初始电平设置 | 帧捕获、DMA |
| 时序控制（DWT_DelayUs） | LCD、FIFO 操作 |

上层 `ov7670.c` 调用 `SCCB_WriteReg()` / `SCCB_ReadReg()` 配置寄存器。

---

## 2. 硬件配置

### 2.1 引脚

| 引脚 | 信号 | GPIO 配置 | 说明 |
|------|------|-----------|------|
| PB10 | OV7670_SCL | OUTPUT_OD, NOPULL, LOW, 初始高 | SCCB 时钟 |
| PB11 | OV7670_SDA | OUTPUT_OD, NOPULL, LOW, 初始高 | SCCB 数据 |

GPIO Mode 由 CubeMX 管理（`.ioc` 已配置）。`SCCB_Init()` 不修改 GPIO Mode，仅设置初始电平。

### 2.2 外部上拉

SCL/SDA 需外接 4.7k-10k 上拉电阻到 VCC。STM32F1 开漏输出模式无内部上拉选项。

### 2.3 OD 输出操作方式

STM32F1 在输出模式下输入缓冲器始终有效，因此采用永久 OD 输出方案：

| 操作 | 实现 | 效果 |
|------|------|------|
| 输出高（释放） | `HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET)` | OD 断开，上拉拉高 |
| 输出低（驱动） | `HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET)` | OD 导通拉低 |
| 读取 | `HAL_GPIO_ReadPin(port, pin)` | 读 IDR 寄存器 |

无需动态切换 INPUT/OUTPUT 模式。

---

## 3. API 设计

```c
/** @brief  Initialize SCCB bus (set pins idle high) */
void    SCCB_Init(void);

/** @brief  Write one byte to OV7670 register
  * @param  reg_addr  Register address (0x00-0xFF)
  * @param  data      Byte to write
  * @retval true   ACK received (slave responded)
  * @retval false  NACK or bus error
  */
bool    SCCB_WriteReg(uint8_t reg_addr, uint8_t data);

/** @brief  Read one byte from OV7670 register
  * @param  reg_addr  Register address (0x00-0xFF)
  * @return Byte read from register (0x00 if bus error)
  */
uint8_t SCCB_ReadReg(uint8_t reg_addr);
```

设备地址 `0x42`（写）/ `0x43`（读）为模块内部常量，不暴露给调用方。

### 3.1 SCCB_Init 职责

- 将 SCL 和 SDA 都置高（`GPIO_PIN_SET`），确保总线空闲态
- 不修改 GPIO Mode（依赖 CubeMX 配置）
- 不使用 `assert_param`（SCCB 在初始化阶段调用，assert 可能依赖未初始化的外设）

---

## 4. SCCB 协议时序

### 4.1 时钟参数

使用 `DWT_DelayUs` 实现半周期延时：

| 阶段 | 延时 | 规格要求 | 状态 |
|------|------|----------|------|
| t_LOW（SCL 低） | 2 us | >= 1.3 us | 满足 |
| t_HIGH（SCL 高） | 1 us | >= 600 ns | 满足 |
| 实际时钟频率 | ~333 kHz | <= 400 kHz | 满足 |

### 4.2 信号时序

**START 条件**（SCL/SDA 均高时，SDA 下降沿）：

```
1. 释放 SDA (SET)          // 确保高
2. 释放 SCL (SET)          // 确保高
3. DWT_DelayUs(1)          // t_HIGH setup
4. 驱动 SDA 低 (RESET)     // SDA 下降沿
5. DWT_DelayUs(1)          // hold time
6. 驱动 SCL 低 (RESET)     // 进入第一个 bit 的 t_LOW
```

**STOP 条件**（SCL 高时，SDA 上升沿）：

```
1. 驱动 SDA 低 (RESET)     // 确保低
2. 驱动 SCL 低 (RESET)     // 确保低
3. DWT_DelayUs(2)          // t_LOW
4. 释放 SCL (SET)          // SCL 高
5. DWT_DelayUs(1)          // t_HIGH setup
6. 释放 SDA (SET)          // SDA 上升沿 = STOP
7. DWT_DelayUs(1)          // hold time
```

**RESTART 条件**：与 START 相同，不 preceded by STOP。

### 4.3 字节写入

```
for each bit (MSB first, 8 bits):
    1. 设置 SDA = bit 值 (SET 或 RESET)    // 数据在 SCL 低时变化
    2. DWT_DelayUs(2)                       // t_LOW + data setup
    3. 释放 SCL (SET)                       // SCL 上升沿，从机采样
    4. DWT_DelayUs(1)                       // t_HIGH
    5. 驱动 SCL 低 (RESET)                  // 准备下一个 bit

第 9 位（ACK）:
    1. 释放 SDA (SET)                       // 主机释放，从机可拉低
    2. DWT_DelayUs(2)                       // t_LOW
    3. 释放 SCL (SET)                       // SCL 高
    4. DWT_DelayUs(1)                       // t_HIGH
    5. 读取 SDA                              // 0 = ACK, 1 = NACK
    6. 驱动 SCL 低 (RESET)
```

返回值：`true` = ACK（SDA == 0），`false` = NACK（SDA == 1）。

### 4.4 字节读取

```
for each bit (MSB first, 8 bits):
    1. 释放 SDA (SET)                       // 主机释放，从机驱动
    2. DWT_DelayUs(2)                       // t_LOW
    3. 释放 SCL (SET)                       // SCL 高
    4. DWT_DelayUs(1)                       // t_HIGH
    5. 读取 SDA，移入 byte                   // 采样
    6. 驱动 SCL 低 (RESET)

第 9 位（ACK/NACK）:
    1. 若 ack == true: 驱动 SDA 低 (RESET)   // 发 ACK（继续读）
       若 ack == false: 释放 SDA (SET)       // 发 NACK（读结束）
    2. DWT_DelayUs(2)                       // t_LOW
    3. 释放 SCL (SET)                       // SCL 高
    4. DWT_DelayUs(1)                       // t_HIGH
    5. 驱动 SCL 低 (RESET)
    6. 释放 SDA (SET)                       // 恢复释放态
```

### 4.5 写寄存器流程

```
START
-> 发送 0x42 (设备地址 + W) -> 检查 ACK
-> 发送 reg_addr            -> 检查 ACK
-> 发送 data                -> 检查 ACK
STOP
```

任一字节 NACK 即发送 STOP 并返回 `false`。

### 4.6 读寄存器流程

```
START
-> 发送 0x42 (设备地址 + W) -> 检查 ACK
-> 发送 reg_addr            -> 检查 ACK
RESTART（与 START 相同，无前置 STOP）
-> 发送 0x43 (设备地址 + R) -> 检查 ACK
-> 读取 data (发 NACK)
STOP
```

写阶段任一字节 NACK 或读阶段设备地址 NACK，发送 STOP 并返回 `0x00`。

---

## 5. 内部函数

以下函数为 `static`，仅在模块内部使用：

```c
/** @brief  Generate START condition */
static void    sccb_start(void);

/** @brief  Generate STOP condition */
static void    sccb_stop(void);

/** @brief  Send one byte and check ACK
  * @param  byte  Data to send (MSB first)
  * @retval true   ACK received
  * @retval false  NACK received
  */
static bool    sccb_write_byte(uint8_t byte);

/** @brief  Read one byte and send ACK/NACK
  * @param  ack  true = send ACK (continue reading), false = send NACK (last byte)
  * @return Byte read from slave
  */
static uint8_t sccb_read_byte(bool ack);
```

---

## 6. 错误处理策略

| 场景 | 处理 |
|------|------|
| WriteReg 中任一字节 NACK | 发 STOP，返回 false |
| ReadReg 写阶段 NACK | 发 STOP，返回 0x00 |
| ReadReg 读阶段 0x43 无响应 | 发 STOP，返回 0x00 |
| 总线初始状态异常 | SCCB_Init 强制 SCL/SDA 置高 |

不使用 `assert_param`。错误通过返回值传递，由上层 `ov7670.c` 决定是否重试或报错。

---

## 7. 文件清单

| 文件 | 说明 |
|------|------|
| `Core/Inc/bsp/ov7670_sccb.h` | 模块头文件，3 个公开 API |
| `Core/Src/bsp/ov7670_sccb.c` | 模块实现，含 4 个 static 函数 + 3 个公开函数 |

需在 `CMakeLists.txt` 注册 `Core/Src/bsp/ov7670_sccb.c`（`Core/Inc/bsp` 已在 include 路径中）。

---

## 8. 设计决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| SDA 方向管理 | 永久 OD 输出 + 读 IDR | STM32F1 输出模式下输入缓冲器始终有效，无需逐 bit 切换模式 |
| 上拉电阻 | 外部 4.7k-10k | STM32F1 OD 输出无内部上拉，外部上拉支持 400kHz 全速 |
| ACK 检查 | 检查并返回 bool | 可在 OV7670 init 时检测摄像头是否在线 |
| GPIO 初始化 | 依赖 CubeMX 配置 | GPIO 配置集中在 MX_GPIO_Init，SCCB_Init 只设初始电平 |
| 时钟频率 | ~333 kHz (t_LOW=2us, t_HIGH=1us) | 满足 SCCB 时序规格，余量充足 |
