#include "SIFAnalyzer.h"
#include "SIFAnalyzerSettings.h"
#include "SIFAnalyzerResults.h"
#include "SIFSimulationDataGenerator.h"
#include <AnalyzerChannelData.h>
#include <cstdio>

// ---------------------------------------------------------------------------
// 构造函数 / 析构函数
// ---------------------------------------------------------------------------
SIFAnalyzer::SIFAnalyzer()
    : Analyzer2()
    , mSettings(new SIFAnalyzerSettings())
    , mResults(nullptr)
    , mSIFChannel(nullptr)
    , mState(SIF_STATE_SEEK_SYNC)
    , mSampleRateHz(0)
    , mEdgeSample(0)
    , mPrevBitState(BIT_LOW)
    , mByteVal(0)
    , mBitCount(0)
    , mByteStartSample(0)
    , mPulse1Width(0)
    , mPulse1Level(BIT_LOW)
    , mHasPulse1(false)
    , mPulseStartSample(0)
{
    SetAnalyzerSettings(mSettings.get());
}

SIFAnalyzer::~SIFAnalyzer()
{
    KillThread();
}

// ---------------------------------------------------------------------------
// 分析器名称
// ---------------------------------------------------------------------------
const char* SIFAnalyzer::GetAnalyzerName() const
{
    return "SIF Protocol";
}

bool SIFAnalyzer::NeedsRerun()
{
    return false;
}

// ---------------------------------------------------------------------------
// 最小采样率
// ---------------------------------------------------------------------------
U32 SIFAnalyzer::GetMinimumSampleRateHz()
{
    return 1000000;  // 1 MHz
}

// ---------------------------------------------------------------------------
// 初始化结果输出
// ---------------------------------------------------------------------------
void SIFAnalyzer::SetupResults()
{
    mResults.reset(new SIFAnalyzerResults(this, mSettings.get()));
    SetAnalyzerResults(mResults.get());

    // 添加通道
    mResults->AddChannelBubblesWillAppearOn(mSettings->mSIFChannel);
}

// ---------------------------------------------------------------------------
// 主工作线程
// ---------------------------------------------------------------------------
void SIFAnalyzer::WorkerThread()
{
    mSampleRateHz = GetSampleRate();
    mSIFChannel = GetAnalyzerChannelData(mSettings->mSIFChannel);

    mState = SIF_STATE_SEEK_SYNC;
    mEdgeSample = 0;
    mPrevBitState = mSIFChannel->GetBitState();
    mHasPulse1 = false;
    mByteVal = 0;
    mBitCount = 0;

    mSIFChannel->AdvanceToNextEdge();
    mEdgeSample = mSIFChannel->GetSampleNumber();
    ReportProgress(mEdgeSample);

    for (;;)
    {
        mSIFChannel->AdvanceToNextEdge();
        U64 next_sample = mSIFChannel->GetSampleNumber();
        U64 pulse_samples = next_sample - mEdgeSample;
        BitState level = mPrevBitState;  // 刚结束的电平

        mPrevBitState = mSIFChannel->GetBitState();
        mEdgeSample = next_sample;

        if (pulse_samples == 0)
            continue;

        double dur = SamplesToSec(pulse_samples);

        switch (mState)
        {
        // ---- SEEK_SYNC: 等待低电平 ≥ 8ms ----
        case SIF_STATE_SEEK_SYNC:
            if (level == BIT_LOW && dur >= SYNC_LOW_MIN_SEC)
            {
                mState = SIF_STATE_SYNC_H;
                mPulseStartSample = mEdgeSample;

                printf("[SIF] SYNC low=%.1f ms\n", dur * 1000.0);

                // 输出 SYNC 帧
                U64 sync_start = mEdgeSample - pulse_samples;
                Frame frame;
                frame.mStartingSampleInclusive = sync_start;
                frame.mEndingSampleInclusive = mEdgeSample;
                frame.mType = SIF_RESULT_SYNC;
                frame.mData1 = (U64)(dur * 10000.0);  // 0.1ms 单位
                mResults->AddFrame(frame);

                mResults->CommitPacketAndStartNewPacket();
            }
            break;

        // ---- SYNC_H: 验证同步高电平 → 进入数据区 ----
        case SIF_STATE_SYNC_H:
            if (level == BIT_HIGH && dur >= SYNC_H_MIN_SEC)
            {
                // 同步高结束(检测到低电平)，进入数据接收
                mState = SIF_STATE_DATA;
                mHasPulse1 = true;
                mPulse1Width = pulse_samples;
                mPulse1Level = level;    // 当前是低电平↓ (数据首脉冲)
                mPulseStartSample = mEdgeSample;
                mByteVal = 0;
                mBitCount = 0;
                printf("[SIF] SYNC high=%.1f ms, enter DATA\n", dur * 1000.0);
            }
            break;

        // ---- DATA: 解码数据位 ----
        case SIF_STATE_DATA:
            if (mHasPulse1)
            {
                U64 high_s, low_s;
                if (mPulse1Level == BIT_HIGH)
                { high_s = mPulse1Width; low_s = pulse_samples; }
                else
                { high_s = pulse_samples; low_s = mPulse1Width; }

                int bit = DecodeBit(high_s, low_s);
                if (bit >= 0)
                {
                    if (mBitCount == 0)
                        mByteStartSample = mPulseStartSample;
                    mByteVal = (mByteVal << 1) | U8(bit);
                    mBitCount++;

                    if (mBitCount >= 8)
                    {
                        printf("[SIF] Byte: 0x%02X\n", mByteVal);
                        Frame frame;
                        frame.mStartingSampleInclusive = mByteStartSample;
                        frame.mEndingSampleInclusive = mEdgeSample;
                        frame.mType = SIF_RESULT_BYTE;
                        frame.mData1 = mByteVal;
                        mResults->AddFrame(frame);
                        mByteVal = 0;
                        mBitCount = 0;
                    }
                }
                mHasPulse1 = false;
            }
            else
            {
                mHasPulse1 = true;
                mPulse1Width = pulse_samples;
                mPulse1Level = level;
                mPulseStartSample = mEdgeSample;
            }

            // 帧结束检测: 低电平 ≥ 2ms, 且在字节边界
            if (level == BIT_LOW && dur >= END_SIGNAL_SEC && mBitCount == 0 && !mHasPulse1)
            {
                printf("[SIF] Frame end (low=%.1f ms)\n", dur * 1000.0);
                mState = SIF_STATE_SEEK_SYNC;
            }
            break;
        }

        ReportProgress(mEdgeSample);
        CheckIfThreadShouldExit();
    }
}

// ---------------------------------------------------------------------------
// 时间转换
// ---------------------------------------------------------------------------
double SIFAnalyzer::SamplesToSec(U64 samples) const
{
    return double(samples) / double(mSampleRateHz);
}

// ---------------------------------------------------------------------------
// 判位: 对齐 MCU —— 直接比较长短，高更长→1，否则→0
// ---------------------------------------------------------------------------
int SIFAnalyzer::DecodeBit(U64 high_s, U64 low_s)
{
    if (high_s > low_s)
        return 1;
    else
        return 0;
}

// ---------------------------------------------------------------------------
// 仿真数据
// ---------------------------------------------------------------------------
U32 SIFAnalyzer::GenerateSimulationData(
    U64 newest_sample_requested,
    U32 sample_rate,
    SimulationChannelDescriptor** simulation_channels)
{
    SIFSimulationDataGenerator sim_gen;
    sim_gen.Initialize(sample_rate, mSettings.get());
    return sim_gen.GenerateSimulationData(
        newest_sample_requested, sample_rate, simulation_channels);
}

// ---------------------------------------------------------------------------
// DLL 导出
// ---------------------------------------------------------------------------
extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName()
{
    return "SIF Protocol";
}

extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer()
{
    return new SIFAnalyzer();
}

extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer(Analyzer* analyzer)
{
    delete analyzer;
}
