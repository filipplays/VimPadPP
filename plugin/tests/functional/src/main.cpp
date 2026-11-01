#include "json_test_cases.h"
#include "test_runner.h"

#include <algorithm>
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

bool ReadTestCases(const std::filesystem::path &sCasesDirectory, std::vector<TestCase> &aTestCases, std::string &sError)
{
    std::error_code sFilesystemError;
    if (!std::filesystem::is_directory(sCasesDirectory, sFilesystemError))
    {
        sError = sFilesystemError ? "could not read test-case directory " + sCasesDirectory.string() + ": " +
                                        sFilesystemError.message()
                                  : "test-case path is not a directory: " + sCasesDirectory.string();
        return false;
    }

    std::vector<std::filesystem::path>                  aCasePaths;
    std::filesystem::recursive_directory_iterator       sIterator(sCasesDirectory, sFilesystemError);
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
            "could not enumerate test-case directory " + sCasesDirectory.string() + ": " + sFilesystemError.message();
        return false;
    }
    std::sort(
        aCasePaths.begin(), aCasePaths.end(),
        [](const std::filesystem::path &sLeft, const std::filesystem::path &sRight)
        { return sLeft.generic_string() < sRight.generic_string(); });
    if (aCasePaths.empty())
    {
        sError = "no JSON test-case files were found under " + sCasesDirectory.string();
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
    if (nArgumentCount != 3)
    {
        std::cerr << "Usage: VimMotionsTests.exe <test-cases-directory> "
                     "<report-directory>\n";
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
    if (!RunFunctionalTests(aTestCases, aResults, sError))
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
