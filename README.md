# SIF Protocol Analyzer for Saleae Logic 2

基于 SIF (Single Inline Frame) 单线通信协议的 C++ Protocol Analyzer。
直接选择数字通道，无需桥接分析器。

## 🚀 安装

1. Logic 2 → Settings → 滑到底部 **Custom Low Level Analyzers**
2. Browse 选择 `SIF_CPP\build\SIFAnalyzer\Release\`
3. 保存 → 重启 Logic 2
4. Analyzers → ＋ → 搜索 **SIF Protocol** → 选择数字通道

## 🔨 编译

**前提**: Visual Studio 2022 + CMake

```
cd SIF_CPP
build.bat
```

脚本自动从本地 Logic 2 的 `Analyzer.dll` 生成 import library。

## 📊 功能

- ✅ 直接选择数字通道
- ✅ 同步识别 (长低 + 短高, 自动 Tosc)
- ✅ 数据解析 (一低一高 = 1 bit, H>L→1)
- ✅ 字节组装 (8 bit → 0xNN)
- ✅ 帧结束检测 (低 ≥ 2ms + 字节边界)
- ✅ 毛刺过滤 (< 80µs 忽略)
- ✅ CSV 导出

## 📁 结构

```
SIF_CPP/
├── build.bat                   # 一键编译
├── CMakeLists.txt
├── AnalyzerSDK/                 # 头文件 + import lib
└── SIFAnalyzer/
    ├── SIFAnalyzer.cpp/h        # 核心状态机
    ├── SIFAnalyzerSettings.cpp/h
    ├── SIFAnalyzerResults.cpp/h
    └── SIFSimulationDataGenerator.cpp/h
```

## 📖 协议

详见 [SIF_protocol_summary.md](SIF_protocol_summary.md)

## ⚠️ 注意

- 采样率建议 ≥ 1 MHz
- 首次编译需 Logic 2 已安装（提取 SDK 符号）
- 更新 Logic 2 后建议重新编译

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