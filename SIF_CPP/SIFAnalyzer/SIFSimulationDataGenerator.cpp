#include "SIFSimulationDataGenerator.h"
#include "SIFAnalyzerSettings.h"
#include "SIFAnalyzer.h"

SIFSimulationDataGenerator::SIFSimulationDataGenerator()
    : mSettings(nullptr)
    , mSimulationSampleRateHz(10000000)
{
}

SIFSimulationDataGenerator::~SIFSimulationDataGenerator()
{
}

void SIFSimulationDataGenerator::Initialize(U32 simulation_sample_rate,
                                             SIFAnalyzerSettings* settings)
{
    mSimulationSampleRateHz = simulation_sample_rate;
    mSettings = settings;
}

U32 SIFSimulationDataGenerator::GenerateSimulationData(
    U64 newest_sample_requested,
    U32 sample_rate,
    SimulationChannelDescriptor** simulation_channels)
{
    U64 samples = 0;

    mSIFSimData = SimulationChannelDescriptor();
    mSIFSimData.SetChannel(mSettings->mSIFChannel);
    mSIFSimData.SetSampleRate(sample_rate);
    mSIFSimData.SetInitialBitState(BIT_HIGH);
    *simulation_channels = &mSIFSimData;

    // Tosc 换算: 默认 32Tosc = 500us → Tosc = 15.625us
    double tosc_us = 15.625;
    U32 tosc_samples = U32(tosc_us * 1e-6 * sample_rate);
    U32 short_pulse = SHORT_PULSE_TOSC * tosc_samples;
    U32 long_pulse  = LONG_PULSE_TOSC * tosc_samples;

    // --- 同步脉冲 (低电平 ~31ms) ---
    U32 sync_samples = U32(0.031 * sample_rate);
    for (U32 i = 0; i < sync_samples && samples < newest_sample_requested; i++)
        mSIFSimData.TransitionIfNeeded(BIT_LOW);
    samples += sync_samples;

    // --- 短校准脉冲 (高电平 32Tosc) ---
    for (U32 i = 0; i < short_pulse && samples < newest_sample_requested; i++)
        mSIFSimData.TransitionIfNeeded(BIT_HIGH);
    samples += short_pulse;

    // --- 数据: "Hello" = 01001000 01100101 ... ---
    const char* message = "Hello";
    while (*message && samples < newest_sample_requested)
    {
        U8 byte_val = U8(*message++);
        for (int b = 7; b >= 0; b--)
        {
            bool bit = (byte_val >> b) & 1;
            CreateSIFBit(mSIFSimData, samples, bit, short_pulse, long_pulse);
            if (samples >= newest_sample_requested)
                break;
        }
    }

    // 填充剩余
    mSIFSimData.TransitionIfNeeded(BIT_HIGH);

    return U32(samples);
}

void SIFSimulationDataGenerator::CreateSIFBit(
    SimulationChannelDescriptor& sim_data,
    U64& samples,
    bool bit_val,
    U32 short_pulse,
    U32 long_pulse)
{
    if (bit_val)
    {
        // Bit 1: 短高 + 长低
        for (U32 i = 0; i < short_pulse; i++)
            sim_data.TransitionIfNeeded(BIT_HIGH);
        samples += short_pulse;
        for (U32 i = 0; i < long_pulse; i++)
            sim_data.TransitionIfNeeded(BIT_LOW);
        samples += long_pulse;
    }
    else
    {
        // Bit 0: 长高 + 短低
        for (U32 i = 0; i < long_pulse; i++)
            sim_data.TransitionIfNeeded(BIT_HIGH);
        samples += long_pulse;
        for (U32 i = 0; i < short_pulse; i++)
            sim_data.TransitionIfNeeded(BIT_LOW);
        samples += short_pulse;
    }
}
