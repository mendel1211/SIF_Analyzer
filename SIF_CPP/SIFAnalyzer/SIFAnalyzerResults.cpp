#include "SIFAnalyzerResults.h"
#include "SIFAnalyzer.h"
#include "SIFAnalyzerSettings.h"
#include <AnalyzerHelpers.h>
#include <sstream>

SIFAnalyzerResults::SIFAnalyzerResults(SIFAnalyzer* analyzer, SIFAnalyzerSettings* settings)
    : AnalyzerResults()
    , mAnalyzer(analyzer)
    , mSettings(settings)
{
}

SIFAnalyzerResults::~SIFAnalyzerResults()
{
}

void SIFAnalyzerResults::GenerateBubbleText(U64 frame_index,
                                             Channel& channel,
                                             DisplayBase display_base)
{
    ClearResultStrings();
    Frame frame = GetFrame(frame_index);

    if (frame.mType == SIF_RESULT_SYNC)
    {
        double dur_ms = double(frame.mData1) / 10.0;
        std::stringstream ss;
        ss << "SYNC " << dur_ms << " ms";
        AddResultString(ss.str().c_str());
    }
    else if (frame.mType == SIF_RESULT_BYTE)
    {
        std::stringstream ss;
        ss << "0x" << std::uppercase << std::hex << U32(frame.mData1);
        AddResultString(ss.str().c_str());

        ss.str("");
        ss << "0x" << std::uppercase << std::hex << U32(frame.mData1)
           << "  (" << std::dec << U32(frame.mData1) << ")";
        AddResultString(ss.str().c_str());
    }
}

void SIFAnalyzerResults::GenerateExportFile(const char* file,
                                             DisplayBase display_base,
                                             U32 export_type_user_id)
{
    std::stringstream ss;
    U64 num_frames = GetNumFrames();

    ss << "Time,Type,Value" << std::endl;
    for (U64 i = 0; i < num_frames; i++)
    {
        Frame frame = GetFrame(i);
        ss << frame.mStartingSampleInclusive << ",";
        if (frame.mType == SIF_RESULT_SYNC)
        {
            ss << "SYNC";
        }
        else if (frame.mType == SIF_RESULT_BYTE)
        {
            ss << "BYTE,0x" << std::hex << frame.mData1;
        }
        ss << std::endl;
    }

    FILE* fp = fopen(file, "w");
    if (fp)
    {
        fwrite(ss.str().c_str(), 1, ss.str().length(), fp);
        fclose(fp);
    }
}

void SIFAnalyzerResults::GeneratePacketTabularText(U64 packet_id,
                                                    DisplayBase display_base)
{
    ClearTabularText();
    AddTabularText("SIF Packet");
}

void SIFAnalyzerResults::GenerateTransactionTabularText(U64 transaction_id,
                                                         DisplayBase display_base)
{
    ClearTabularText();
    AddTabularText("SIF Transaction");
}

void SIFAnalyzerResults::GenerateFrameTabularText(U64 frame_index,
                                                    DisplayBase display_base)
{
    ClearTabularText();
    Frame frame = GetFrame(frame_index);

    if (frame.mType == SIF_RESULT_SYNC)
    {
        double dur_ms = double(frame.mData1) / 10.0;
        std::stringstream ss;
        ss << "SYNC  " << dur_ms << " ms";
        AddTabularText(ss.str().c_str());
    }
    else if (frame.mType == SIF_RESULT_BYTE)
    {
        std::stringstream ss;
        ss << "0x" << std::uppercase << std::hex << U32(frame.mData1);
        AddTabularText(ss.str().c_str());
    }
}
