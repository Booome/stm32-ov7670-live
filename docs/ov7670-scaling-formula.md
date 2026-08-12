# OV7670 缩放公式参考

## 来源

- **官方文档**：`OV7670_Implementation_Guide_V1.0.pdf` Section 6.4 (page 40)
- **datasheet**：`OV7670_DS_(1_4).pdf` register table (page 21-22)

## 缩放架构

OV7670 的 Image Scaler 由两级组成：

```
VGA (640x480) -> Down Sampling -> Digital Zoom Out -> 输出
                  (DCW)            (XSC/YSC)
```

- **Down Sampling**：支持 1/2、1/4、1/8 降采样（整数倍）
- **Digital Zoom Out**：支持 1x ~ 0.5x 的分数缩放

## 寄存器定义

### COM3 (0x0C)

| Bit | 功能 | 0 | 1 |
|-----|------|---|---|
| [3] | Digital Zoom Enable | Bypass | Enable |
| [2] | Down Sampling Enable | Disable | Enable |

### COM14 (0x3E)

| Bit | 功能 |
|-----|------|
| [4] | DCW/scaling PCLK enable (0=Normal, 1=controlled by COM14[2:0] + SCALING_PCLK_DIV) |
| [3] | Manual scaling enable (0=不可手动调, 1=可手动调 XSC/YSC) |
| [2:0] | PCLK divider (000=/1, 001=/2, 010=/4, ...) |

### DCWCTR (0x72) - Down Sampling 控制

| Bit | 功能 |
|-----|------|
| [7] | Vertical average calculation (0=truncation, 1=rounding) |
| [6] | Vertical down sampling option (0=truncation, 1=rounding) |
| [5:4] | Vertical down sampling rate (00=none, 01=by2, 10=by4, 11=by8) |
| [3] | Horizontal average calculation (0=truncation, 1=rounding) |
| [2] | Horizontal down sampling option (0=truncation, 1=rounding) |
| [1:0] | Horizontal down sampling rate (00=none, 01=by2, 10=by4, 11=by8) |

### XSC (0x70) / YSC (0x71) - Digital Zoom Out 缩放系数

| Bit | 功能 |
|-----|------|
| [7] | test_pattern bit (XSC[7]=test_pattern[0], YSC[7]=test_pattern[1]) |
| [6:0] | Scaling factor (水平/垂直) |

**test_pattern 定义**（需要 COM7 bit1 + COM17 bit3 使能 colorbar）：

| (XSC7, YSC7) | 图案 |
|--------------|------|
| 00 | 无测试输出 |
| 01 | Shifting "1" |
| **10** | **8-bar color bar** |
| 11 | Fade to gray color bar |

## 缩放公式（IG Section 6.4 官方公式）

```
XSC[6:0] = 0x20 × DownSample_Width  / Target_Width
YSC[6:0] = 0x20 × DownSample_Height / Target_Height
```

其中：
- `0x20` = 32 = 1.0x 基准（不缩放时写入 0x20）
- `0x40` = 64 = 0.5x（缩放到一半）
- `DownSample_Width/Height` = Down Sampling 电路输出的尺寸
- `Target_Width/Height` = 最终输出尺寸

**注意**：XSC/YSC 的 bit[7] 是 test_pattern，与 bit[6:0] 的 scale factor 独立。
设置 colorbar 时，XSC = scale_factor | 0x80，YSC = scale_factor | 0x80（根据需要的 tp 组合）。

## Pixel Clock Divider (0x73)

根据总缩放比例选择 PCLK 分频：

| 总缩放比例 | PCLK/Byte | SCALING_PCLK_DIV[3:0] |
|-----------|-----------|----------------------|
| 1x ~ 1/2x | 1 | 0x0 |
| 1/2x ~ 1/4x | 2 | 0x1 |
| 1/4x ~ 1/8x | 4 | 0x2 |
| 1/8x ~ 1/16x | 8 | 0x3 |

## Pixel Clock Delay (0xA2)

当新尺寸不是原始阵列分辨率的整数倍时，需要补偿时序偏移：

```
Pixel clock delay = (Original H size / Pixel clock divider) - New H size
```

## 本项目配置验证 (160x128 RGB565)

### 缩放流水线

| 步骤 | 水平 | 垂直 | 寄存器 |
|------|------|------|--------|
| VGA 原始 | 640 | 480 | 传感器阵列 |
| DCW by 2 (H+V) | 320 | 240 | DCWCTR=0x11 (bit[5:4]=01, bit[1:0]=01) |
| DSP Zoom Out | 160 | 128 | XSC=0x40, YSC=0x3C |
| **输出** | **160** | **128** | ✓ |

### 缩放系数计算

```
XSC = 0x20 × 320 / 160 = 32 × 2.0  = 64 = 0x40  ✓
YSC = 0x20 × 240 / 128 = 32 × 1.875 = 60 = 0x3C  ✓
```

### 寄存器配置表

| 地址 | 寄存器 | 值 | 说明 |
|------|--------|-----|------|
| 0x0C | COM3 | 0x0C | bit3=1 Zoom enable, bit2=1 DCW enable |
| 0x3E | COM14 | 0x18 | bit4=1 scaling PCLK, bit3=1 manual scaling |
| 0x72 | DCWCTR | 0x11 | V by2 + H by2 |
| 0x70 | XSC | 0x40 | H: 320->160 (0.5x) |
| 0x71 | YSC | 0x3C | V: 240->128 (0.533x) |
| 0x73 | PCLK_DIV | 0xF1 | bit3=0 enable divider, bit[2:0]=001 /2 |
| 0xA2 | PCLK_DELAY | 0x02 | 时序补偿 |

### test_pattern 配置

启用 8-bar color bar 时，需要额外设置：

| 寄存器 | 普通 | tp=10 (8-bar) | 说明 |
|--------|------|---------------|------|
| COM7 (0x12) | 0x14 | 0x16 | bit1=1 colorbar enable |
| COM17 (0x42) | 0x00 | 0x08 | bit3=1 DSP colorbar enable |
| XSC (0x70) | 0x40 | 0x40 | bit7=0, tp[0]=0 |
| YSC (0x71) | 0x3C | 0xBC | bit7=1, tp[1]=1 |

**注意**：tp=10 表示 (XSC7=0, YSC7=1)，即 YSC 的 bit7 置 1，XSC 的 bit7 保持 0。

## 其他分辨率示例

### 256x128 (IG 官方例子)

```
Down Sampling: 640x480 -> 320x240 (by 2 H+V, DCWCTR=0x11)
Digital Zoom:  320x240  -> 256x128
  XSC = 0x20 × 320 / 256 = 40 = 0x28
  YSC = 0x20 × 240 / 128 = 60 = 0x3C  (与 160x128 相同的垂直缩放)
```

### 320x240 (QVGA, IG 官方配置)

```
Down Sampling: 640x480 -> 320x240 (by 2 H+V, DCWCTR=0x11)
Digital Zoom:  320x240  -> 320x240 (不缩放)
  XSC = 0x20 × 320 / 320 = 32 = 0x20 (1.0x)
  YSC = 0x20 × 240 / 240 = 32 = 0x20 (1.0x)
  COM3=0x04, COM14=0x19
```

### 160x120 (QQVGA, IG 官方配置)

```
Down Sampling: 640x480 -> 160x120 (by 4 H+V, DCWCTR=0x22)
Digital Zoom:  160x120  -> 160x120 (不缩放)
  XSC = 0x20 × 160 / 160 = 32 = 0x20 (1.0x)
  YSC = 0x20 × 120 / 120 = 32 = 0x20 (1.0x)
  COM3=0x04, COM14=0x1A
```

## 自定义分辨率计算步骤

1. 确定目标尺寸 (Target_W, Target_H)
2. 选择 Down Sampling 倍率 (1/2, 1/4, 1/8)，使 DownSample 尺寸 >= Target 尺寸
   - DCWCTR bit[5:4] = V rate, bit[1:0] = H rate
   - DownSample_W = 640 / H_rate, DownSample_H = 480 / V_rate
3. 计算 XSC/YSC：
   - XSC = 0x20 × DownSample_W / Target_W
   - YSC = 0x20 × DownSample_H / Target_H
4. 设置 PCLK_DIV：根据总缩放比例 (VGA->Target) 选择
5. 计算 PCLK_DELAY：Original_H / PCLK_div - Target_W
6. 如需 colorbar，设置 XSC/YSC 的 bit[7] 为 test_pattern 值
