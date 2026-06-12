cmake_minimum_required(VERSION 3.14)
project(SIFAnalyzer)

# 使用本地 SDK stub（GitHub 不可达时的备选）
set(ANALYZER_SDK_DIR "${CMAKE_SOURCE_DIR}/AnalyzerSDK")

add_subdirectory(SIFAnalyzer)
