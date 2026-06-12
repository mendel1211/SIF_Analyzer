#ifndef SIF_ANALYZER_H
#define SIF_ANALYZER_H

#include <Analyzer.h>
#include "SIFAnalyzerResults.h"

// ---------------------------------------------------------------------------
// 前向声明
// ---------------------------------------------------------------------------
class SIFAnalyzerSettings;
class SIFAnalyzer;

// ---------------------------------------------------------------------------
// 协议常量（Tosc 单位）
// ---------------------------------------------------------------------------
const U32 SYNC_THRESHOLD_TOSC = 992;
const U32 SHORT_PULSE_TOSC = 32;
const U32 LONG_PULSE_TOSC = 64;
const double RATIO_THRESHOLD = 1.2;
const double SYNC_MIN_SEC = 0.030;

// ---------------------------------------------------------------------------
// 分析器状态
// ---------------------------------------------------------------------------
enum SIFState
{
    SIF_STATE_SEEK_SYNC,
    SIF_STATE_CALIBRATE,
    SIF_STATE_DATA
};

// ---------------------------------------------------------------------------
// 分析器
// ---------------------------------------------------------------------------
class SIFAnalyzer : public Analyzer2
{
public:
    SIFAnalyzer();
    virtual ~SIFAnalyzer();

    virtual void SetupResults();
    virtual void WorkerThread();

    virtual U32 GenerateSimulationData(U64 newest_sample_requested,
                                       U32 sample_rate,
                                       SimulationChannelDescriptor** simulation_channels);
    virtual U32 GetMinimumSampleRateHz();

    virtual const char* GetAnalyzerName() const;
    virtual bool NeedsRerun();

#pragma warning(push)
#pragma warning(disable : 4251)
protected:
    std::unique_ptr<SIFAnalyzerSettings>  mSettings;
    std::unique_ptr<SIFAnalyzerResults>   mResults;
    AnalyzerChannelData*                   mSIFChannel;

    // 状态
    SIFState  mState;
    double    mTosc;          // 检测到的 Tosc（秒）

    U64       mSampleRateHz;  // 采样率
    U64       mEdgeSample;    // 当前沿的采样号
    BitState  mPrevBitState;  // 前一个电平

    // 字节缓冲
    U8        mByteVal;       // 当前字节值
    U32       mBitCount;      // 已收集位数
    U64       mByteStartSample;

    // 脉冲缓冲
    U64       mPulse1Width;   // 第一个脉冲宽度（采样数）
    BitState  mPulse1Level;   // 第一个脉冲电平
    bool      mHasPulse1;
    U64       mPulseStartSample;

    // 辅助方法
    double    SamplesToSec(U64 samples) const;
    U64       SecToSamples(double sec) const;
    bool      IsSync(U64 low_samples);
    SIFState  SeekSync(U64 low_samples);
    SIFState  Calibrate(U64 pulse_samples, BitState level);
    int       DecodeBit(U64 high_samples, U64 low_samples);
    void      EmitByte(U64 start_sample, U64 end_sample);

#pragma warning(pop)
};

// 导出函数
extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();

#endif
