@echo off
call "D:\Visual Studio2022\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
cd /d "c:\Users\Fan\Documents\SIF_Analyser\SIF_CPP"

REM Generate import lib from Logic 2's Analyzer.dll (if not already generated)
if not exist AnalyzerSDK\Analyzer.lib (
    echo Generating Analyzer.lib from Logic 2 Analyzer.dll...
    dumpbin /exports "D:\Logic\resources\windows-x64\Analyzer.dll" /out:AnalyzerSDK\exports.txt >nul 2>&1
    powershell -Command "$e=gc AnalyzerSDK\exports.txt;$s=$false;$n=@();foreach($l in $e){if($l -match 'Summary'){break};if($s -and $l -match '^\s+\d+\s+\w+\s+\w+\s+(.+)$'){$n+=$matches[1]};if($l -match 'ordinal hint'){$s=$true}};'EXPORTS'|Out-File AnalyzerSDK\Analyzer.def -Enc ASCII;foreach($x in $n){$x|Out-File AnalyzerSDK\Analyzer.def -Enc ASCII -Append}"
    lib /def:AnalyzerSDK\Analyzer.def /machine:x64 /out:AnalyzerSDK\Analyzer.lib >nul 2>&1
    echo Done.
)

if not exist build mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release --parallel
echo.
echo ============================================
echo Build complete!
echo Output: build\SIFAnalyzer\Release\SIFAnalyzer.dll
