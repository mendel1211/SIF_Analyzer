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
// 协议常量 (MD 协议摘要 + MCU 帧结束)
// ---------------------------------------------------------------------------
const double SYNC_LOW_MIN_SEC  = 0.008;   // 同步低 ≥ 8ms
const double END_SIGNAL_SEC    = 0.002;   // 帧结束 ≥ 2ms (MCU)
const double MIN_PULSE_SEC     = 0.00008; // 最小有效脉冲 80µs (过滤毛刺)
const U32   TOSC_UNITS_SHORT   = 32;      // 短脉冲 Tosc 单位

// ---------------------------------------------------------------------------
// 状态机
// ---------------------------------------------------------------------------
enum SIFState
{
    SIF_STATE_SEEK_SYNC,
    SIF_STATE_SYNC_H,
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
    double    mTosc;           // 同步高测得的 Tosc (秒), 0=未测

    U64       mSampleRateHz;
    U64       mEdgeSample;
    BitState  mPrevBitState;

    // 字节缓冲
    U8        mByteVal;
    U32       mBitCount;
    U64       mByteStartSample;

    // 脉冲缓冲 — 一低一高 = 1 bit
    U64       mPulse1Width;
    bool      mHasPulse1;
    U64       mPulseStartSample;

    // 辅助
    double    SamplesToSec(U64 samples) const;
    double    ToscSec() const { return (mTosc > 0.0) ? mTosc : 15.625e-6; }

#pragma warning(pop)
};

// 导出函数
extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();

#endif
