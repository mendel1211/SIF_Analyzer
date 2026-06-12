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
// 协议常量（对齐 MCU sif.c 实现）
// ---------------------------------------------------------------------------
const double SYNC_LOW_MIN_SEC  = 0.008;  // 同步低电平 ≥ 8ms (MCU 10ms, 留余量)
const double END_SIGNAL_SEC    = 0.002;  // 帧结束信号 ≥ 2ms
const double SYNC_H_MIN_SEC    = 0.0003; // 同步高电平最小 ~0.3ms
const double SYNC_H_MAX_SEC    = 0.003;  // 同步高电平最大 ~3ms (放宽)

// ---------------------------------------------------------------------------
// 分析器状态 (对齐 MCU: INITIAL→SYNC_L→SYNC_H→DATA)
// ---------------------------------------------------------------------------
enum SIFState
{
    SIF_STATE_SEEK_SYNC,   // INITIAL: 等待同步低电平
    SIF_STATE_SYNC_H,      // SYNC_L→SYNC_H: 验证同步高电平
    SIF_STATE_DATA         // DATA_RX: 解码数据
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

    U64       mSampleRateHz;
    U64       mEdgeSample;
    BitState  mPrevBitState;

    // 字节缓冲
    U8        mByteVal;
    U32       mBitCount;
    U64       mByteStartSample;

    // 脉冲缓冲
    U64       mPulse1Width;
    BitState  mPulse1Level;
    bool      mHasPulse1;
    U64       mPulseStartSample;

    // 辅助
    double    SamplesToSec(U64 samples) const;
    int       DecodeBit(U64 high_s, U64 low_s);

#pragma warning(pop)
};

// 导出函数
extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();

#endif
