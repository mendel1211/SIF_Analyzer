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

### 物理层解析
- ✅ 直接选择数字通道
- ✅ 同步识别 — 长低电平(≥8ms) + 短高脉冲(~32Tosc)
- ✅ 自动 Tosc 测量 — 从同步高脉宽推算时钟基准
- ✅ 数据位解码 — 一低一高 = 1 bit, 高长于低 → 1, 低长于高 → 0
- ✅ 字节组装 — 8 bit → 0xNN (MSB first)
- ✅ 帧结束检测 — 低电平 ≥ 2ms + 字节边界对齐
- ✅ 毛刺过滤 — < 80µs 脉冲忽略
- ✅ CSV 导出

### TAILG 二层解析
- ✅ Project 下拉选择 Raw / TAILG 模式
- ✅ 帧头识别 — D0 01 前缀自动定位 TLV 包
- ✅ TAG/LEN 配对 — 交替标注每个 TLV 的 Tag 和 Length
- ✅ 变长数据跳过 — 根据 LEN 自动跳过 N 字节
- ✅ SUMCRC 校验 — D0 起逐字节累加，取低 8 位与 CRC 比对
- ✅ 校验结果 — 正确显示 `SUMCRC 0xNN`，错误显示 `CRCERR 0xNN(exp 0xYY)`

### 气泡标注 (TAILG 模式)
| 类型 | 显示 | 颜色 |
|------|------|------|
| Sync | `SYNC 10.0 ms` | 绿 |
| Raw Byte | `0xNN` | 白 |
| TAG | `TAG 0xNN` | 青 |
| LEN | `LEN=N` | 黄 |
| CRC 正确 | `SUMCRC 0xNN` | 绿 |
| CRC 错误 | `CRCERR 0xNN(exp 0xYY)` | 红 |

## 📁 结构

```
SIF_CPP/
├── build.bat                     # 一键编译
├── CMakeLists.txt
├── AnalyzerSDK/                   # SDK 头文件 + 自动生成的 import lib
└── SIFAnalyzer/
    ├── SIFAnalyzer.cpp/h          # 核心状态机 + TAILG + SUMCRC
    ├── SIFAnalyzerSettings.cpp/h   # 通道/阈值/Project 设置
    ├── SIFAnalyzerResults.cpp/h   # 气泡/表格/导出
    └── SIFSimulationDataGenerator.cpp/h
```

## ⚙️ 设置项

| 设置 | 说明 |
|------|------|
| **SIF Signal** | 选择信号通道 |
| **Sync Threshold (Tosc)** | 同步脉冲最小宽度 (默认 992 Tosc) |
| **Project** | Raw = 仅字节解析 / TAILG = D0+01+TLV+SUMCRC |

## ⚠️ 注意

- 采样率建议 ≥ 1 MHz
- 首次编译需 Logic 2 已安装 (提取 Analyzer.dll 符号)
- 更新 Logic 2 后建议重新编译
- 编译前请关闭 Logic 2 (否则 DLL 被占用)

## 📖 协议详情

详见 [SIF_protocol_summary.md](SIF_protocol_summary.md)

---

**版本**: 1.0.0  
**作者**: SIF Protocol Team  
**最后更新**: 2026-06-12