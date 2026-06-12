add_library(SIFAnalyzer SHARED
    SIFAnalyzer.cpp
    SIFAnalyzerSettings.cpp
    SIFAnalyzerResults.cpp
    SIFSimulationDataGenerator.cpp
)

target_include_directories(SIFAnalyzer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/AnalyzerSDK
)

# 链接 Logic 2 的 SDK import library（从 Analyzer.dll 生成）
target_link_libraries(SIFAnalyzer PRIVATE "${CMAKE_SOURCE_DIR}/AnalyzerSDK/Analyzer.lib")

# UTF-8
target_compile_options(SIFAnalyzer PRIVATE /utf-8)
set_target_properties(SIFAnalyzer PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)
