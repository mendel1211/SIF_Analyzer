#ifndef SIF_ANALYZER_RESULTS_H
#define SIF_ANALYZER_RESULTS_H

#include <AnalyzerResults.h>

enum SIFResultType
{
    SIF_RESULT_SYNC,
    SIF_RESULT_BYTE
};

class SIFAnalyzer;
class SIFAnalyzerSettings;

class SIFAnalyzerResults : public AnalyzerResults
{
public:
    SIFAnalyzerResults(SIFAnalyzer* analyzer, SIFAnalyzerSettings* settings);
    virtual ~SIFAnalyzerResults();

    virtual void GenerateBubbleText(U64 frame_index,
                                    Channel& channel,
                                    DisplayBase display_base);
    virtual void GenerateExportFile(const char* file,
                                    DisplayBase display_base,
                                    U32 export_type_user_id);
    virtual void GenerateFrameTabularText(U64 frame_index,
                                          DisplayBase display_base);
    virtual void GeneratePacketTabularText(U64 packet_id,
                                           DisplayBase display_base);
    virtual void GenerateTransactionTabularText(U64 transaction_id,
                                                DisplayBase display_base);

protected:
    SIFAnalyzer*         mAnalyzer;
    SIFAnalyzerSettings* mSettings;
};

#endif
