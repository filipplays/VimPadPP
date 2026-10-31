#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct TestCase
{
    std::string              sId;
    std::string              sInitialText;
    LRESULT                  nInitialCaret = 0;
    std::vector<std::string> aKeys;
    std::string              sExpectedText;
    LRESULT                  nExpectedCaret = 0;
};

struct TestResult
{
    const TestCase *pTestCase = nullptr;
    bool            bPassed   = false;
    std::string     sActualText;
    LRESULT         nActualCaret         = 0;
    long long       nElapsedMicroseconds = 0;
    std::string     sError;
};
