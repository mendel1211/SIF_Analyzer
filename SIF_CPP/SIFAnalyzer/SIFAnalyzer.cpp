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
    , mTosc(0.0)
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

    // 初始化
    mState = SIF_STATE_SEEK_SYNC;
    mTosc = 0.0;
    mEdgeSample = 0;
    mPrevBitState = mSIFChannel->GetBitState();
    mHasPulse1 = false;
    mByteVal = 0;
    mBitCount = 0;

    // 跳到第一个沿
    mSIFChannel->AdvanceToNextEdge();
    mEdgeSample = mSIFChannel->GetSampleNumber();
    ReportProgress(mEdgeSample);

    // 调试输出
    U64 debug_frame_count = 0;

    for (;;)
    {
        // 到下一个沿
        mSIFChannel->AdvanceToNextEdge();
        U64 next_sample = mSIFChannel->GetSampleNumber();
        U64 pulse_samples = next_sample - mEdgeSample;
        BitState level = mPrevBitState;  // 刚结束的电平

        mPrevBitState = mSIFChannel->GetBitState();
        mEdgeSample = next_sample;

        if (pulse_samples == 0)
            continue;

        switch (mState)
        {
        case SIF_STATE_SEEK_SYNC:
            if (level == BIT_LOW && IsSync(pulse_samples))
            {
                mState = SIF_STATE_CALIBRATE;
                mPulseStartSample = mEdgeSample;

                // 输出 SYNC 标记
                U64 sync_start = mEdgeSample - pulse_samples;
                mResults->AddMarker(sync_start, AnalyzerResults::Start, mSettings->mSIFChannel);
                mResults->AddMarker(mEdgeSample, AnalyzerResults::Stop, mSettings->mSIFChannel);

                double dur_ms = SamplesToSec(pulse_samples) * 1000.0;
                mResults->CommitPacketAndStartNewPacket();

                // 设置帧数据
                Frame frame;
                frame.mStartingSampleInclusive = sync_start;
                frame.mEndingSampleInclusive = mEdgeSample;
                frame.mType = SIF_RESULT_SYNC;
                frame.mData1 = (U64)(dur_ms * 10.0);  // 存 0.1ms 单位
                mResults->AddFrame(frame);
            }
            break;

        case SIF_STATE_CALIBRATE:
            // 用第一个脉冲估算 Tosc，但不把它当数据
            if (mTosc == 0.0 && pulse_samples > 0)
            {
                double pulse_sec = SamplesToSec(pulse_samples);
                mTosc = pulse_sec / double(SHORT_PULSE_TOSC);
                printf("[SIF] Tosc=%.3f us, 32T=%.0f us\n",
                       mTosc * 1e6, mTosc * SHORT_PULSE_TOSC * 1e6);
            }
            mState = SIF_STATE_DATA;
            mHasPulse1 = false;  // 下一个脉冲才是数据起始
            break;

        case SIF_STATE_DATA:
            if (mHasPulse1)
            {
                // 第二个脉冲 — 判位
                U64 high_samples, low_samples;
                if (mPulse1Level == BIT_HIGH)
                {
                    high_samples = mPulse1Width;
                    low_samples = pulse_samples;
                }
                else
                {
                    high_samples = pulse_samples;
                    low_samples = mPulse1Width;
                }

                int bit = DecodeBit(high_samples, low_samples);
                if (bit >= 0)
                {
                    // 拼字节
                    if (mBitCount == 0)
                        mByteStartSample = mPulseStartSample;
                    mByteVal = (mByteVal << 1) | U8(bit);
                    mBitCount++;

                    if (mBitCount >= 8)
                    {
                        printf("[SIF] Byte: 0x%02X\n", mByteVal);
                        // 输出 byte
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
                // 第一个脉冲
                mHasPulse1 = true;
                mPulse1Width = pulse_samples;
                mPulse1Level = level;
                mPulseStartSample = mEdgeSample;
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

U64 SIFAnalyzer::SecToSamples(double sec) const
{
    return U64(sec * double(mSampleRateHz));
}

// ---------------------------------------------------------------------------
// 同步识别
// ---------------------------------------------------------------------------
bool SIFAnalyzer::IsSync(U64 low_samples)
{
    double dur = SamplesToSec(low_samples);
    double tosc = (mTosc > 0.0) ? mTosc : (15.625e-6);
    double threshold = double(SYNC_THRESHOLD_TOSC) * tosc;
    if (threshold < SYNC_MIN_SEC)
        threshold = SYNC_MIN_SEC;
    return dur > threshold;
}

// ---------------------------------------------------------------------------
// 1.2 倍规则判位
// ---------------------------------------------------------------------------
int SIFAnalyzer::DecodeBit(U64 high_samples, U64 low_samples)
{
    double ht = double(high_samples);
    double lt = double(low_samples);

    if (lt > RATIO_THRESHOLD * ht)
        return 0;
    else if (ht > RATIO_THRESHOLD * lt)
        return 1;
    return -1;
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
