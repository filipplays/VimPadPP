#include "json_test_cases.h"
#include "test_runner.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
bool ReadFile(const std::filesystem::path &sPath, std::string &sContents, std::string &sError)
{
    std::ifstream sFile(sPath, std::ios::binary);
    if (!sFile)
    {
        sError = "could not open " + sPath.string();
        return false;
    }
    sContents.assign(std::istreambuf_iterator<char>(sFile), std::istreambuf_iterator<char>());
    if (!sFile.good() && !sFile.eof())
    {
        sError = "could not read " + sPath.string();
        return false;
    }
    return true;
}

bool WriteReport(const std::filesystem::path &sPath, const std::string &sContents, std::string &sError)
{
    std::ofstream sFile(sPath, std::ios::binary);
    if (!sFile)
    {
        sError = "could not write " + sPath.string();
        return false;
    }
    sFile << sContents;
    if (!sFile)
    {
        sError = "could not finish writing " + sPath.string();
        return false;
    }
    return true;
}

bool ReadTestCases(const std::filesystem::path &sCasesPath, std::vector<TestCase> &aTestCases, std::string &sError)
{
    std::error_code                    sFilesystemError;
    std::vector<std::filesystem::path> aCasePaths;
    if (std::filesystem::is_regular_file(sCasesPath, sFilesystemError))
    {
        aCasePaths.push_back(sCasesPath);
    }
    else if (!sFilesystemError && std::filesystem::is_directory(sCasesPath, sFilesystemError))
    {
        std::filesystem::recursive_directory_iterator       sIterator(sCasesPath, sFilesystemError);
        const std::filesystem::recursive_directory_iterator sEnd;
        while (!sFilesystemError && sIterator != sEnd)
        {
            const bool bIsRegularFile = sIterator->is_regular_file(sFilesystemError);
            if (sFilesystemError)
            {
                break;
            }
            if (bIsRegularFile && sIterator->path().extension() == ".json")
            {
                aCasePaths.push_back(sIterator->path());
            }
            sIterator.increment(sFilesystemError);
        }
        if (sFilesystemError)
        {
            sError =
                "could not enumerate test-case directory " + sCasesPath.string() + ": " + sFilesystemError.message();
            return false;
        }
    }
    else
    {
        sError = "test-case path is not a file or directory: " + sCasesPath.string();
        if (sFilesystemError)
            sError += ": " + sFilesystemError.message();
        return false;
    }
    std::sort(
        aCasePaths.begin(), aCasePaths.end(),
        [](const std::filesystem::path &sLeft, const std::filesystem::path &sRight)
        { return sLeft.generic_string() < sRight.generic_string(); });
    if (aCasePaths.empty())
    {
        sError = "no JSON test-case files were found under " + sCasesPath.string();
        return false;
    }

    aTestCases.clear();
    for (const std::filesystem::path &sCasePath : aCasePaths)
    {
        std::string sJson;
        if (!ReadFile(sCasePath, sJson, sError))
        {
            return false;
        }
        try
        {
            std::vector<TestCase> aFileTestCases = ParseTestCasesJson(sJson);
            aTestCases.insert(aTestCases.end(), aFileTestCases.begin(), aFileTestCases.end());
        }
        catch (const std::exception &sException)
        {
            sError = "could not parse " + sCasePath.string() + ": " + sException.what();
            return false;
        }
    }
    return true;
}
} // namespace

int main(int nArgumentCount, char *aArguments[])
{
    bool          bVisible          = false;
    std::uint32_t nWaitMilliseconds = 0;
    bool          bValidArguments   = nArgumentCount >= 3;
    for (int nIndex = 3; bValidArguments && nIndex < nArgumentCount; ++nIndex)
    {
        const std::string sArgument = aArguments[nIndex];
        if (sArgument == "--visible")
            bVisible = true;
        else if (sArgument == "--wait-ms" && nIndex + 1 < nArgumentCount)
        {
            const std::string sWait   = aArguments[++nIndex];
            const auto        sParsed = std::from_chars(sWait.data(), sWait.data() + sWait.size(), nWaitMilliseconds);
            bValidArguments = !sWait.empty() && sParsed.ec == std::errc{} && sParsed.ptr == sWait.data() + sWait.size();
        }
        else
            bValidArguments = false;
    }
    if (!bValidArguments)
    {
        std::cerr << "Usage: VimMotionsTests.exe <test-case-file-or-directory> "
                     "<report-directory> [--visible] [--wait-ms <milliseconds>]\n";
        return 2;
    }

    std::string           sError;
    std::vector<TestCase> aTestCases;
    if (!ReadTestCases(aArguments[1], aTestCases, sError))
    {
        std::cerr << "Test setup failed: " << sError << '\n';
        return 2;
    }
    if (aTestCases.empty())
    {
        std::cerr << "Test setup failed: no test cases were defined\n";
        return 2;
    }

    std::vector<TestResult> aResults;
    if (!RunFunctionalTests(aTestCases, aResults, sError, bVisible, nWaitMilliseconds))
    {
        std::cerr << "Test setup failed: " << sError << '\n';
        return 2;
    }

    const std::string sTextReport = BuildTextReport(aResults);
    const std::string sJsonReport = BuildJsonReport(aResults);
    std::cout << sTextReport;

    const std::filesystem::path sReportDirectory(aArguments[2]);
    std::error_code             sFilesystemError;
    std::filesystem::create_directories(sReportDirectory, sFilesystemError);
    if (sFilesystemError || !WriteReport(sReportDirectory / "functional-report.txt", sTextReport, sError) ||
        !WriteReport(sReportDirectory / "functional-report.json", sJsonReport, sError))
    {
        std::cerr << "Could not write test report: " << sError << '\n';
        return 2;
    }

    for (const TestResult &sResult : aResults)
    {
        if (!sResult.bPassed)
        {
            return 1;
        }
    }
    return 0;
}
