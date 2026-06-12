#include "SIFAnalyzerSettings.h"
#include "SIFAnalyzer.h"
#include <AnalyzerHelpers.h>

SIFAnalyzerSettings::SIFAnalyzerSettings()
    : mSyncThresholdTosc(SYNC_THRESHOLD_TOSC)
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
    ClearChannels();
    AddChannel(mSIFChannel, "SIF", true);
    return true;
}

void SIFAnalyzerSettings::UpdateInterfacesFromSettings()
{
    mChannelInterface->SetChannel(mSIFChannel);
    mSyncThresholdInterface->SetInteger(mSyncThresholdTosc);
}

void SIFAnalyzerSettings::LoadSettings(const char* settings)
{
    SimpleArchive archive;
    archive.SetString(settings);
    archive >> mSIFChannel;
    archive >> mSyncThresholdTosc;
    ClearChannels();
    AddChannel(mSIFChannel, "SIF", true);
    UpdateInterfacesFromSettings();
}

const char* SIFAnalyzerSettings::SaveSettings()
{
    SimpleArchive archive;
    archive << mSIFChannel;
    archive << mSyncThresholdTosc;
    return SetReturnString(archive.GetString());
}
