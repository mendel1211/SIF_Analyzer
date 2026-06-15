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
    mTosc = 0.0;
    mEdgeSample = 0;
    mPrevBitState = mSIFChannel->GetBitState();
    mHasPulse1 = false;
    mByteVal = 0;
    mBitCount = 0;
    mTailgPos = 0; mTailgSkip = 0; mTailgTotal = 0; mTailgSum = 0; mTailgNextIsLen = false;

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

        // 毛刺过滤: < 80µs 忽略 (MCU 有边沿去抖)
        if (dur < MIN_PULSE_SEC)
            continue;

        switch (mState)
        {
        // ---- SEEK_SYNC: 长低 → 同步 ----
        case SIF_STATE_SEEK_SYNC:
            if (level == BIT_LOW && dur >= SYNC_LOW_MIN_SEC)
                mState = SIF_STATE_SYNC_H;
            break;

        // ---- SYNC_H: 高脉冲 → 测 Tosc → 数据开始 ----
        case SIF_STATE_SYNC_H:
            if (level == BIT_HIGH && dur >= 0.0001 && dur < 0.005)
            {
                mTosc = dur / double(TOSC_UNITS_SHORT);
                mState = SIF_STATE_DATA;
                mHasPulse1 = false;
                mByteVal = 0; mBitCount = 0;
                mTailgPos = 0; mTailgSkip = 0; mTailgTotal = 0; mTailgSum = 0; mTailgNextIsLen = false;
                printf("[SIF] Tosc=%.1f us -> DATA\n", ToscSec() * 1e6);
            }
            break;

        // ---- DATA: 一低一高=1bit, 8bit=1byte ----
        case SIF_STATE_DATA:
        {
            // 新同步
            if (level == BIT_LOW && dur >= SYNC_LOW_MIN_SEC)
            {
                mState = SIF_STATE_SYNC_H;
                mHasPulse1 = false;
                mByteVal = 0; mBitCount = 0;
                mTailgPos = 0; mTailgSkip = 0; mTailgTotal = 0; mTailgSum = 0; mTailgNextIsLen = false;
                break;
            }
            // 帧结束
            if (level == BIT_LOW && dur >= END_SIGNAL_SEC
                && mBitCount == 0 && !mHasPulse1)
            {
                mState = SIF_STATE_SEEK_SYNC;
                mTailgPos = 0; mTailgSkip = 0; mTailgTotal = 0; mTailgSum = 0; mTailgNextIsLen = false;
                break;
            }

            if (mHasPulse1)
            {
                // 第二个脉冲(高) → bit完整, 判位: 高长→1, 低长→0
                double low_ms  = SamplesToSec(mPulse1Width) * 1000.0;
                double high_ms = dur * 1000.0;
                int bit = (high_ms > low_ms) ? 1 : 0;

                if (mBitCount == 0)
                    mByteStartSample = mPulseStartSample;
                mByteVal = (mByteVal << 1) | U8(bit);
                mBitCount++;

                if (mBitCount >= 8)
                {
                    U8  ftype = SIF_RESULT_BYTE;

                    // TAILG: D0+01+总长+TLV+CRC. CRC位置=总长+3
                    if (mSettings->mProjectIndex == 1)
                    {
                        if (mTailgPos == 0 && mByteVal == 0xD0)
                            { mTailgSum = mByteVal; }
                        else if (mTailgPos == 1 && mByteVal == 0x01)
                            { mTailgSum += mByteVal; }
                        else if (mTailgPos == 2)
                            { mTailgTotal = 4 + mByteVal; mTailgSum += mByteVal; }
                        else if (mTailgPos >= 3)
                        {
                            if (mTailgPos + 1 == mTailgTotal)
                            {
                                // CRC: 校验 SUM(D0..CRC前)
                                U8 sum8 = U8(mTailgSum & 0xFF);
                                if (sum8 == mByteVal)
                                    { ftype = SIF_RESULT_TLV_CRC; }
                                else
                                    { ftype = SIF_RESULT_TLV_CRCERR; }
                                mTailgSum = sum8;  // 暂存期望值供 Frame 使用
                            }
                            else
                            {
                                mTailgSum += mByteVal;
                                if (mTailgSkip > 0)
                                    { mTailgSkip--; if (mTailgSkip == 0) mTailgNextIsLen = false; }
                                else if (!mTailgNextIsLen)
                                    { ftype = SIF_RESULT_TLV_TAG; mTailgNextIsLen = true; }
                                else
                                    { mTailgSkip = mByteVal; ftype = SIF_RESULT_TLV_LEN; mTailgNextIsLen = false; }
                            }
                        }
                        mTailgPos++;
                    }

                    Frame f;
                    f.mStartingSampleInclusive = mByteStartSample;
                    f.mEndingSampleInclusive = mEdgeSample;
                    f.mType = ftype; f.mData1 = mByteVal;
                    if (ftype == SIF_RESULT_TLV_CRC || ftype == SIF_RESULT_TLV_CRCERR)
                        f.mData2 = mTailgSum;  // 期望的 SUM 校验值
                    mResults->AddFrame(f);
                    mByteVal = 0; mBitCount = 0;
                }
                mHasPulse1 = false;
            }
            else
            {
                // 第一个脉冲(低) → 暂存
                mHasPulse1 = true;
                mPulse1Width = pulse_samples;
                mPulseStartSample = mEdgeSample - pulse_samples;
            }
            break;
        }
        }  // end switch (mState)

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
