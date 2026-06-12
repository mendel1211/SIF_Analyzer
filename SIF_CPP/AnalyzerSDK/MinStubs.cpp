// 最小构造函数桩 —— 初始化 PIMPL 指针避免野指针崩溃
// 注意：AnalyzerChannelData 没有默认构造函数（工厂模式），不要实现它

#include <Analyzer.h>
#include <AnalyzerResults.h>
#include <AnalyzerHelpers.h>
#include <AnalyzerSettings.h>
#include <AnalyzerSettingInterface.h>

// -- Analyzer --
Analyzer::Analyzer() : mData(nullptr) {}
Analyzer::~Analyzer() {}

// -- Analyzer2 --
Analyzer2::Analyzer2() : Analyzer() {}

// -- AnalyzerResults --
AnalyzerResults::AnalyzerResults() {}
AnalyzerResults::~AnalyzerResults() {}

// -- AnalyzerSettings --
AnalyzerSettings::AnalyzerSettings() {}
AnalyzerSettings::~AnalyzerSettings() {}

// -- AnalyzerSettingInterface (PIMPL: mData) --
AnalyzerSettingInterface::AnalyzerSettingInterface() : mData(nullptr) {}
AnalyzerSettingInterface::~AnalyzerSettingInterface() {}

// -- AnalyzerSettingInterfaceChannel (PIMPL: mChannelData) --
AnalyzerSettingInterfaceChannel::AnalyzerSettingInterfaceChannel() : mChannelData(nullptr) {}
AnalyzerSettingInterfaceChannel::~AnalyzerSettingInterfaceChannel() {}

// -- SimulationChannelDescriptor (PIMPL: mData) --
SimulationChannelDescriptor::SimulationChannelDescriptor() : mData(nullptr) {}
SimulationChannelDescriptor::~SimulationChannelDescriptor() {}
