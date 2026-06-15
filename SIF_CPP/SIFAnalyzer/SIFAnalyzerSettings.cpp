#include "SIFAnalyzerSettings.h"
#include <AnalyzerHelpers.h>

SIFAnalyzerSettings::SIFAnalyzerSettings()
    : mSyncThresholdTosc(992)
    , mProjectIndex(0)
{
    mChannelInterface.reset(new AnalyzerSettingInterfaceChannel());
    mChannelInterface->SetTitleAndTooltip("SIF Signal", "SIF single-wire signal");
    mChannelInterface->SetChannel(mSIFChannel);
    AddInterface(mChannelInterface.get());

    mSyncThresholdInterface.reset(new AnalyzerSettingInterfaceInteger());
    mSyncThresholdInterface->SetTitleAndTooltip(
        "Sync Threshold (Tosc)",
        "Minimum sync pulse width in Tosc units (default 992)");
    mSyncThresholdInterface->SetMax(10000);
    mSyncThresholdInterface->SetMin(100);
    mSyncThresholdInterface->SetInteger(mSyncThresholdTosc);
    AddInterface(mSyncThresholdInterface.get());

    mProjectInterface.reset(new AnalyzerSettingInterfaceNumberList());
    mProjectInterface->SetTitleAndTooltip("Project", "Protocol variant");
    mProjectInterface->AddNumber(0, "Raw (bytes only)", "");
    mProjectInterface->AddNumber(1, "TAILG (0xD0+0x01+TLV)", "");
    mProjectInterface->SetNumber(mProjectIndex);
    AddInterface(mProjectInterface.get());

    AddExportOption(0, "Export as text/csv file");
    AddExportExtension(0, "csv", "csv");
    AddExportExtension(0, "txt", "txt");

    ClearChannels();
    AddChannel(mSIFChannel, "SIF", true);
    UpdateInterfacesFromSettings();
}

SIFAnalyzerSettings::~SIFAnalyzerSettings()
{
}

bool SIFAnalyzerSettings::SetSettingsFromInterfaces()
{
    mSIFChannel = mChannelInterface->GetChannel();
    mSyncThresholdTosc = U32(mSyncThresholdInterface->GetInteger());
    mProjectIndex = U32(mProjectInterface->GetNumber());
    ClearChannels();
    AddChannel(mSIFChannel, "SIF", true);
    return true;
}

void SIFAnalyzerSettings::UpdateInterfacesFromSettings()
{
    mChannelInterface->SetChannel(mSIFChannel);
    mSyncThresholdInterface->SetInteger(mSyncThresholdTosc);
    mProjectInterface->SetNumber(mProjectIndex);
}

void SIFAnalyzerSettings::LoadSettings(const char* settings)
{
    SimpleArchive archive;
    archive.SetString(settings);
    archive >> mSIFChannel;
    archive >> mSyncThresholdTosc;
    archive >> mProjectIndex;
    ClearChannels();
    AddChannel(mSIFChannel, "SIF", true);
    UpdateInterfacesFromSettings();
}

const char* SIFAnalyzerSettings::SaveSettings()
{
    SimpleArchive archive;
    archive << mSIFChannel;
    archive << mSyncThresholdTosc;
    archive << mProjectIndex;
    return SetReturnString(archive.GetString());
}
