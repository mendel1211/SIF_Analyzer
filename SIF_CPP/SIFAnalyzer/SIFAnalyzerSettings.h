#ifndef SIF_ANALYZER_SETTINGS_H
#define SIF_ANALYZER_SETTINGS_H

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

class SIFAnalyzerSettings : public AnalyzerSettings
{
public:
    SIFAnalyzerSettings();
    virtual ~SIFAnalyzerSettings();

    virtual bool SetSettingsFromInterfaces();
    virtual void LoadSettings(const char* settings);
    virtual const char* SaveSettings();

    void UpdateInterfacesFromSettings();

    Channel mSIFChannel;
    U32     mSyncThresholdTosc;
    U32     mProjectIndex;     // 0=Raw, 1=TAILG

protected:
    std::unique_ptr<AnalyzerSettingInterfaceChannel>    mChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceInteger>    mSyncThresholdInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mProjectInterface;
};

#endif
