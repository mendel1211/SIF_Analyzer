# SIF Protocol Analyzer for Saleae Logic 2

基于 SIF (Single Inline Frame) 单线通信协议的 **C++ Protocol Analyzer**。

## 🚀 安装

```
1. Logic 2 → Settings → Custom Low Level Analyzers
2. 选择目录: SIF_CPP\build\SIFAnalyzer\Release\
3. 重启 Logic 2
4. Analyzers → + → "SIF Protocol" → 直接选通道
```

## 🔨 编译

```
cd SIF_CPP && build.bat   (需要 VS 2022)
```

## 📁 结构

```
SIF_CPP/
├── build.bat              # 编译脚本
├── CMakeLists.txt          # 根 CMake
├── AnalyzerSDK/            # Saleae SDK 头文件 + import lib
└── SIFAnalyzer/
    ├── SIFAnalyzer.cpp     # 核心：同步识别 → Tosc测量 → 1.2倍判位
    ├── SIFAnalyzer.h
    ├── SIFAnalyzerSettings.cpp/h  # 通道选择 + 参数
    ├── SIFAnalyzerResults.cpp/h   # 气泡显示 + CSV导出
    └── SIFSimulationDataGenerator.cpp/h  # 模拟数据
```

## 📊 协议

详见 [SIF_protocol_summary.md](SIF_protocol_summary.md)

## 文件说明

| 文件 | 说明 |
|------|------|
| **SIFHLA.py** | ⭐ 核心分析器（HLA） |
| **extension.json** | Saleae 扩展配置 |
| DigitalMeasurement.py | Range Measurement 版本（备用） |
| ProtocolAnalyzer.py | 高级解码器框架 |
| Config.py | 参数配置管理 |
| Verify.py | 独立验证脚本 |

---

## 协议详情

详见 [SIF_protocol_summary.md](SIF_protocol_summary.md)
|------|------|------|
| Sync Signal Detected | SYNC | 是否检测到同步信号 |
| Tosc (Timing Unit) | Tosc | 测量到的基准时间单位 |
| Decoded Bit Stream | BITS | 解码的比特流和字节值 |
| Bit Errors | ERR | 检测到的位错误数量 |

## 使用方法

### 安装
1. 在 Saleae Logic 中，进入 **Options > Extensions**
2. 点击 **Add Extension**
3. 选择此项目的根目录

### 配置
1. 打开逻辑分析仪并采集 SIF 协议信号
2. 创建新的**数字测量**
3. 选择 **SIF Protocol Analyzer**
4. 分析器会自动进行：
   - 同步信号检测
   - Tosc 测量
   - 数据位解析

## 协议规范参考

### 帧结构
```
[同步脉冲: >992Tosc] 
→ [校准间隔: >32Tosc]
→ [数据位 1-N]
```

### 脉冲编码（以 32Tosc=500µs 为例）
- 短脉冲：500 ± 100 µs（允许 ±20%）
- 长脉冲：1000 ± 200 µs（允许 ±20%）

### 判位规则
- 比较每个数据位内高低电平的持续时间
- 计算比值 = 高电平时间 / 低电平时间
- 若比值 < 1/1.2 ≈ 0.833 → Bit 0
- 否则 → Bit 1

## 文件说明

- **DigitalMeasurement.py**: 核心分析逻辑
  - 同步识别算法
  - Tosc 测量
  - 数据位解码
  
- **SIF_Analyzer.py**: 高级分析器框架（可选扩展）

- **extension.json**: Saleae Logic 扩展配置

## 调试与验证

### 查看原始数据
1. 在 Saleae Logic 中查看采样数据
2. 观察脉冲宽度（用户可查看时间标签）
3. 验证脉冲序列是否符合协议

### 检查测量结果
分析完成后，查看测量窗口中的指标：
- **SYNC**: 确认找到了同步信号
- **Tosc**: 验证测量到的时间单位是否合理
- **BITS**: 查看解码的比特流
- **ERR**: 检查是否有异常

## 协议详情

详见 [SIF_protocol_summary.md](SIF_protocol_summary.md)

---

**版本**: 1.0.0  
**作者**: SIF Protocol Team  
**最后更新**: 2026-06-12