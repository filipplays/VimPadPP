#pragma once

#include "TestCase.h"

#include <cstdint>
#include <string>
#include <vector>

bool RunFunctionalTests(
    const std::vector<TestCase> &aTestCases, std::vector<TestResult> &aResults, std::string &sError,
    bool bVisible = false, std::uint32_t nWaitMilliseconds = 0);
std::string BuildTextReport(const std::vector<TestResult> &aResults);
std::string BuildJsonReport(const std::vector<TestResult> &aResults);
