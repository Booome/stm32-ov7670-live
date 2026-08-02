# OV7670 单元测试设计文档（TEST_SCCB + TEST_OV7670）

> 本文档定义 OV7670 摄像头模块首次上电后的两组硬件在环测试：`TEST_SCCB`（SCCB 总线与接线验证）与 `TEST_OV7670`（初始化后寄存器回读校验）。同时包含为支持可诊断失败而做的 SCCB 读接口增强设计。
>
> 说明：按项目约定，本批次**不实现** `TEST_LOGIC`（纯逻辑组），仅聚焦两组硬件在环测试。

## 1. 概述

### 1.1 背景

这是第一次插上 OV7670 硬件模块。最大不确定性是硬件连接是否正确：SCCB 总线（SCL=PB10 / SDA=PB11）、摄像头电源（PWDN/RESET）、以及寄存器配置是否真正写入生效。

现有驱动栈：
- `ov7670_sccb.c/h`：GPIO bit-bang SCCB，提供 `SCCB_WriteReg()`（返回 ACK 状态）与 `SCCB_ReadReg()`（无错误区分，失败统一返回 0x00）
- `ov7670.c/h`：`OV7670_Init()`（上电时序 + 软复位 + 约 90 项寄存器写表）、`OV7670_EnableColorBar()` / `DisableColorBar()`
- `pipeline.c/h`：双 DMA ping-pong 帧流水线（不在本批测试范围内）

### 1.2 目标

1. 验证 SCCB 总线与接线正确（摄像头身份寄存器 PID/MID 可读、值匹配）
2. 验证 `OV7670_Init()` 完整执行后，关键配置寄存器读回正确
3. 通过状态码区分失败模式（SDA 拉死 / 无响应 / 值异常），便于首次上电排障

### 1.3 非目标

- 不验证 pipeline（Camera DMA / SPI DMA / 帧数据通路）——后续单独设计
- 不实现 TEST_LOGIC 纯逻辑组
- 不做图像质量 / 帧率评估

### 1.4 关键前提（已核实）

- `MX_GPIO_Init()` 中 PWDN 初始化为 `GPIO_PIN_RESET`（上电）、RESET 初始化为 `GPIO_PIN_SET`（释放复位）→ 摄像头在 CubeMX 初始化后即处于可通信状态
- `SCCB_ReadReg()` 当前**无生产代码调用者**，可直接改签名，无需兼容层
- OV7670 部分寄存器为只写或读回带影子状态（如 AGC/AEC 相关），读回值不等于写入值，需按"稳定寄存器"筛选

## 2. SCCB 增强接口设计

### 2.1 目标

让 `SCCB_ReadReg()` 具备失败模式区分能力，使测试能把"接线坏"与"摄像头无响应"分开定位。

### 2.2 失败模式与检测信号

| 失败模式 | 物理表现 | 检测手段 |
|---|---|---|
| **SDA 拉死 / 总线忙** | SDA 被拉到低电平，AC K 时隙读到低 → **假 ACK**，读回全 0x00 | 事务开始前空闲检测：SDA 必须为高，否则返回 BUS_BUSY |
| **设备地址无响应** | 摄像头没上电 / SCL 或 SDA 断线 / 地址不符，ACK 时隙 SDA 保持高 → **NACK** | 首个 `WriteByte(SCCB_DEV_ADDR_W)` 返回 false → NACK_ADDR |
| **寄存器地址 NACK** | 写寄存器地址阶段无响应 | 第二个 `WriteByte(reg_addr)` 返回 false → NACK_REG |
| **读地址 NACK** | RESTART 后读地址阶段无响应 | 第三个 `WriteByte(SCCB_DEV_ADDR_R)` 返回 false → NACK_RADDR |

> **关键点**：SDA 拉死会伪装成"假 ACK"，仅靠 ACK 位无法与"正常"区分。可靠判据是事务开始前的空闲检测（开漏 + 上拉总线空闲时必须为高）。该检测成本极低（一次读 pin），且与 NACK 正交。

### 2.3 接口定义

**策略**：保持生产 API `SCCB_ReadReg` 不变，新增测试专用 `SCCB_ReadRegEx` 带完整状态诊断。符合嵌入式最佳实践：生产 API 最小化，测试 API 独立。

`Core/Inc/bsp/ov7670_sccb.h`：

```c
/* ---- SCCB read status codes ---- */
typedef enum
{
  SCCB_READ_OK = 0,        /**< Transfer complete, data valid */
  SCCB_READ_BUS_BUSY,      /**< SDA stuck low / bus not idle */
  SCCB_READ_NACK_ADDR,     /**< Device address NACK (no response) */
  SCCB_READ_NACK_REG,      /**< Register address NACK */
  SCCB_READ_NACK_RADDR     /**< Read address NACK */
} SCCB_ReadStatusTypeDef;

/* ---- Public API (unchanged) ---- */

/** @brief  Read one byte from OV7670 register (production API, unchanged)
  * @param  reg_addr  Register address (0x00-0xFF)
  * @retval Byte read from register (0x00 if bus error)
  */
uint8_t SCCB_ReadReg(uint8_t reg_addr);

/* ---- Test API (new) ---- */

/** @brief  Read one byte from OV7670 register
  * @param  reg_addr  Register address (0x00-0xFF)
  * @param  data      Pointer to store read byte (valid only on SCCB_READ_OK)
  * @retval SCCB_READ_OK          Transfer complete, data valid
  * @retval SCCB_READ_BUS_BUSY    SDA stuck low before transaction
  * @retval SCCB_READ_NACK_ADDR   Device address NACK
  * @retval SCCB_READ_NACK_REG    Register address NACK
  * @retval SCCB_READ_NACK_RADDR  Read address NACK
  */
SCCB_ReadStatusTypeDef SCCB_ReadRegEx(uint8_t reg_addr, uint8_t *data);
```

`Core/Src/bsp/ov7670_sccb.c` 实现要点：

```c
/* ---- Production API (unchanged) ---- */
uint8_t SCCB_ReadReg(uint8_t reg_addr)
{
  uint8_t data = 0u;

  GenStart();
  if (!WriteByte(SCCB_DEV_ADDR_W)) goto cleanup;
  if (!WriteByte(reg_addr))        goto cleanup;
  GenStart();  /* RESTART */
  if (!WriteByte(SCCB_DEV_ADDR_R)) goto cleanup;
  data = ReadByte(false);  /* NACK = last byte */

cleanup:
  GenStop();
  return data;
}

/* ---- Test API (new, with full status diagnostics) ---- */
SCCB_ReadStatusTypeDef SCCB_ReadRegEx(uint8_t reg_addr, uint8_t *data)
{
  SCCB_ReadStatusTypeDef status = SCCB_READ_OK;

  /* Bus idle check: SDA must be high before starting */
  if (SCCB_SDA_Read() == GPIO_PIN_RESET)
  {
    return SCCB_READ_BUS_BUSY;
  }

  GenStart();
  if (!WriteByte(SCCB_DEV_ADDR_W))
  {
    status = SCCB_READ_NACK_ADDR;
    goto cleanup;
  }
  if (!WriteByte(reg_addr))
  {
    status = SCCB_READ_NACK_REG;
    goto cleanup;
  }
  GenStart();  /* RESTART */
  if (!WriteByte(SCCB_DEV_ADDR_R))
  {
    status = SCCB_READ_NACK_RADDR;
    goto cleanup;
  }
  *data = ReadByte(false);  /* NACK = last byte */

cleanup:
  GenStop();
  return status;
}
```

> 注意：`SCCB_ReadRegEx` 的 `*data` 仅在返回 `SCCB_READ_OK` 时有效。两个函数共享底层 `GenStart`/`WriteByte`/`ReadByte`，无逻辑重复。

### 2.4 身份寄存器地址宏

当前 `ov7670.c` 内的 `#define OV7670_REG_*` 均为内部宏（不在头文件），且 PID/MID 未定义。在 `ov7670_sccb.h` 补充只读身份寄存器宏（SCCB 层读身份寄存器最合适）：

```c
/* OV7670 read-only identity registers */
#define SCCB_REG_PID    0x0Au   /**< Product ID (expected 0x76)    */
#define SCCB_REG_VER    0x0Bu   /**< Version ID (expected 0x73)    */
#define SCCB_REG_MIDH   0x1Cu   /**< Manufacturer ID high (0x7A)   */
#define SCCB_REG_MIDL   0x1Du   /**< Manufacturer ID low  (0xA2)   */
```

## 3. TEST_SCCB 详细设计

### 3.1 目标

验证 SCCB 总线与接线。这是首次上硬件的第一道关卡——总线不通则一切后续测试无意义。

### 3.2 前置依赖

测试入口自行初始化（测试模式下 BSP 初始化被跳过）：

```c
DWT_Init();     /* SCCB 时序依赖 DWT CYCCNT */
SCCB_Init();    /* 总线置空闲高 */
```

**不做** PWDN/RESET 脉冲：CubeMX 已把摄像头置于上电可通信状态。TEST_SCCB 验证的就是"上电后最小通路"，若强加上电时序反而混淆"总线坏"与"时序坏"的定位。

### 3.3 测试用例

| 用例 | 操作 | 断言 |
|---|---|---|
| `test_sccb_read_pid` | `ReadRegEx(SCCB_REG_PID)` | status==OK 且 data==0x76 |
| `test_sccb_read_ver` | `ReadRegEx(SCCB_REG_VER)` | status==OK 且 data==0x73（严格相等） |
| `test_sccb_read_midh` | `ReadRegEx(SCCB_REG_MIDH)` | status==OK 且 data==0x7A |
| `test_sccb_read_midl` | `ReadRegEx(SCCB_REG_MIDL)` | status==OK 且 data==0xA2 |
| `test_sccb_read_stability` | 连续读 PID 5 次 | 全部 status==OK 且 5 次值一致 |

> VER(0x0B) 严格断言 0x73：如果实际模块读到其他版本号导致误报，属可接受——记录到排查指引中，先跑一次看实际值。

### 3.4 失败输出

每个寄存器用例失败时，通过 `debug_printf` 输出状态码字符串：

```c
static const char *StatusToString(SCCB_ReadStatusTypeDef s)
{
  switch (s)
  {
    case SCCB_READ_OK:         return "OK";
    case SCCB_READ_BUS_BUSY:   return "BUS_BUSY (SDA stuck low / bus not idle)";
    case SCCB_READ_NACK_ADDR:  return "NACK_ADDR (device not responding)";
    case SCCB_READ_NACK_REG:   return "NACK_REG (reg addr NACK)";
    case SCCB_READ_NACK_RADDR: return "NACK_RADDR (read addr NACK)";
    default:                   return "?";
  }
}
```

配合 TEST_LED 反馈：全部通过 → 常亮；任一失败 → 快闪。

## 4. TEST_OV7670 详细设计

### 4.1 目标

验证 `OV7670_Init()` 完整执行（上电时序 + 软复位 + 约 90 项寄存器写入）后，关键配置寄存器读回正确。

### 4.2 前置依赖与流程

```c
DWT_Init();
if (!OV7670_Init())          /* 失败则短路，见 4.5 */
{
  TEST_FAIL();
  return;
}
DWT_DelayMs(500u);           /* 传感器上电稳定 + 配置生效等待 */
```

### 4.3 寄存器筛选（核心难点）

**不是所有寄存器读回值都等于写入值**。OV7670 部分寄存器是只写（读回为 0 或垃圾）或带影子状态 / 自动更新（AGC/AEC 随时间变化）。只对"读回稳定"寄存器断言精确值，不稳定的只断 status==OK。

**缩放与方向寄存器**（关键）：OV7670 输出分辨率与方向取决于缩放和 MVFP 寄存器，必须与 LCD（160x128）和 PCB 布局匹配。若后续调整摄像头方向或缩放比例，只需修改 ov7670.c 写表并更新本表期望值。
| 寄存器 | 地址 | 写入值 | 类型 | 断言策略 |
|---|---|---|---|---|
| COM7 | 0x12 | 0x14 | 读写（bit7 RESET 读回为 0） | 精确 == 0x14 |
| COM15 | 0x40 | 0xD0 | 读写 | 精确 == 0xD0 |
| COM3 | 0x0C | 0x0C | 读写 | 精确 == 0x0C |
| MVFP | 0x1E | 0x07 | 读写（方向：镜像/翻转） | 精确 == 0x07 |
| RGB444 | 0x8C | 0x00 | 读写 | 精确 == 0x00 |
| SCALING_DCWCTR | 0x72 | 0x11 | 读写（V/H 各÷2） | 精确 == 0x11 |
| SCALING_XSC | 0x70 | 0x40 | 读写（水平 0.5x: 320→160） | 精确 == 0x40 |
| SCALING_YSC | 0x71 | 0x3C | 读写（垂直 0.533x: 240→128） | 精确 == 0x3C |
| SCALING_PCLK_DIV | 0xF1 | 0xF1 | 读写（时钟÷2） | 精确 == 0xF1 |
| MTX1 | 0x4F | 0xB3 | 读写 | 精确 == 0xB3 |
| GAM0 | 0x7A | 0x20 | 读写 | 精确 == 0x20 |
| COM8 | 0x13 | 0xE7 | 含 AGC/AEC 自动位 | 仅 status==OK |
| GAIN/AECH/HAECC* | — | — | 自动曝光/增益动态变化 | 仅 status==OK |

### 4.4 断言策略（已确认）

- **优先精确相等**：稳定寄存器直接 `TEST_ASSERT_EQUAL_UINT8(write_val, read_val)`
- **读回不稳定的寄存器**：掩码比较（如 `(read & mask) == expected`)）或仅断 status==OK
- 每个寄存器用例统一模式：先 `TEST_ASSERT_EQUAL(SCCB_READ_OK, status)`，再比较值

### 4.5 Init 失败短路

`OV7670_Init()` 内部任一寄存器 SCCB NACK 即返回 false。此时打印明确的 NACK 诊断并跳过后续寄存器回读，避免连环失败掩盖根因。可增强：Init 内部 `SCCB_WriteReg` 失败时已可定位到具体寄存器（`ov7670.c` 的写入循环可加失败地址打印——若需改动，作为可选增强）。

### 4.6 ColorBar 用例（已确认加入）

`OV7670_EnableColorBar()` / `DisableColorBar()` 只改 COM3 bit0，纯寄存器级验证。**不依赖光照 / 镜头 / 图像通路**，能证明传感器本身在正常输出。用于区分"传感器寄存器通路 OK 但画面黑"（→ 图像通路问题）vs"传感器本身异常"。

```c
#define TEST_COM3  0x0Cu   /* OV7670 COM3 address (ov7670.c internal macro, define locally) */

void test_ov7670_colorbar(void)
{
  uint8_t v;

  OV7670_EnableColorBar();
  SCCB_ReadRegEx(TEST_COM3, &v);
  TEST_ASSERT_NOT_EQUAL(0, v & 0x01u);  /* bit0 == 1 */

  OV7670_DisableColorBar();
  SCCB_ReadRegEx(TEST_COM3, &v);
  TEST_ASSERT_EQUAL(0, v & 0x01u);      /* bit0 == 0 */
}
```

> 注意：`OV7670_REG_COM3` 等寄存器宏定义在 `ov7670.c` 内部（非头文件），测试文件无法引用。test_ov7670.c 需在测试文件内用字面量 + 局部 `#define`（如 `#define TEST_COM3 0x0Cu`）或直接写数值，并注释地址含义。

## 5. 文件结构

### 5.1 修改

| 文件 | 变更 |
|---|---|
| `Core/Inc/bsp/ov7670_sccb.h` | 新增 `SCCB_ReadStatusTypeDef` 枚举、身份寄存器宏、`SCCB_ReadRegEx` 声明 |
| `Core/Src/bsp/ov7670_sccb.c` | 新增 `SCCB_ReadRegEx` 实现（空闲检测 + 三阶段 NACK 细分 + data 出参）；`SCCB_ReadReg` 不变 |

### 5.2 新建

| 文件 | 责任 |
|---|---|
| `Core/Inc/test/test_sccb.h` | TEST_SCCB 组声明（`RunSccbTests()`） |
| `Core/Src/test/test_sccb.c` | TEST_SCCB 组实现（5 个用例 + status 字符串映射） |
| `Core/Inc/test/test_ov7670.h` | TEST_OV7670 组声明（`RunOv7670Tests()`） |
| `Core/Src/test/test_ov7670.c` | TEST_OV7670 组实现（稳定寄存器精确断言 + ColorBar + Init 短路） |

### 5.3 已就绪无需改动

- `Core/Src/test/test_runner.c`：已含 `#ifdef TEST_SCCB / TEST_OV7670` 的 include 与 dispatch stub（`RunSccbTests()` / `RunOv7670Tests()`）
- `CMakeLists.txt`：`TEST_SCCB`、`TEST_OV7670` option 与 source 注册已存在（第 111-118 行）
- `Core/Inc/test/test_runner.h`：`g_test_failures` 计数已定义

## 6. CMake 集成

CMake 集成已就绪，无需修改：

```cmake
option(TEST_SCCB     "Enable SCCB hardware-in-loop tests"      OFF)
option(TEST_OV7670   "Enable OV7670 register tests"            OFF)

if(TEST_SCCB)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_sccb.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_SCCB)
endif()
if(TEST_OV7670)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_ov7670.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_OV7670)
endif()
```

**警告**：CMake 会缓存 `-DTEST_*` 的值。每次切换必须显式传 `-D` 并重新 configure，勿假设"没传就是 OFF"。查看当前状态：`grep TEST_ build/Debug/CMakeCache.txt`。

## 7. 构建验证命令

```bash
# TEST_SCCB 组
cmake --preset Debug -DTEST_SCCB=ON && cmake --build --preset Debug

# TEST_OV7670 组
cmake --preset Debug -DTEST_OV7670=ON && cmake --build --preset Debug

# 组合（test_runner 顺序执行两组，TEST_OV7670 内部重复跑 SCCB 初始化无害，不去重）
cmake --preset Debug -DTEST_SCCB=ON -DTEST_OV7670=ON && cmake --build --preset Debug

# 回到正常主程序
cmake --preset Debug -DTEST_SCCB=OFF -DTEST_OV7670=OFF && cmake --build --preset Debug
```

烧录后通过 USART1 (115200) 查看 Unity 报告，TEST_LED 指示通过（常亮）/失败（快闪）。

## 8. 排查指引

首次上电排障决策树（按状态码）：

| 状态码 | 含义 | 排查动作 |
|---|---|---|
| `BUS_BUSY` | SDA 在事务开始前为低（被拉死 / 总线忙） | 查 SDA 是否短路到 GND；查 SCL/SDA 接线是否接反；确认外部上拉存在 |
| `NACK_ADDR` | 设备地址 0x42 无响应 | 查摄像头是否上电（PWDN=低）；查 SCL/SDA 是否断线；查 VCC/GND 供电是否到位 |
| `NACK_REG` | 寄存器地址写阶段 NACK | 少见；多为芯片内部异常，重试或换芯片 |
| `NACK_RADDR` | RESTART 读地址 NACK | 同上 |
| `OK` 但 PID≠0x76 | 读到设备非 OV7670 或版本异常 | 核对模块型号；检查是 OV7670 还是 OV7675/OV7660 等衍生型号 |
| `OK` 但 MID 异常 | PID 对、MID 错 | 多为接触不良 / 虚接，稳定性用例可复现；重新插拔模块 |
| 稳定性用例失败 | 5 次读值不一致 | 接线虚接 / 总线毛刺 / 供电抖动，检查杜邦线接触与电源滤波 |
| Init 返回 false | 写表过程中某个寄存器 NACK | 定位失败寄存器地址（见 4.5 可选增强） |
| Init OK 但回读值不对 | 寄存器被后续写入覆盖 / 影子状态 | 核对寄存器是否为只写或自动更新型（参考 4.3 筛选表） |
| ColorBar 用例失败 | COM3 bit0 无法置/清零 | 寄存器通路异常（写表成功但位未生效），需逐寄存器排查 |
| ColorBar 通过但画面黑 | 传感器通路 OK | 问题在图像数据通路（HREF/PCLK/D0-D7/FIFO/pipeline），不属本批测试范围 |
