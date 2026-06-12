#ifndef SIF_SIMULATION_DATA_GENERATOR_H
#define SIF_SIMULATION_DATA_GENERATOR_H

#include <AnalyzerHelpers.h>

class SIFAnalyzerSettings;
class SIFSimulationDataGenerator
{
public:
    SIFSimulationDataGenerator();
    ~SIFSimulationDataGenerator();

    void Initialize(U32 simulation_sample_rate, SIFAnalyzerSettings* settings);
    U32  GenerateSimulationData(U64 newest_sample_requested,
                                U32 sample_rate,
                                SimulationChannelDescriptor** simulation_channels);

protected:
    SIFAnalyzerSettings*       mSettings;
    U32                        mSimulationSampleRateHz;
    SimulationChannelDescriptor mSIFSimData;

    void CreateSIFBit(SimulationChannelDescriptor& sim_data,
                      U64& samples,
                      bool bit_val,
                      U32 short_tosc,
                      U32 long_tosc);
};
#endif
