#pragma once

#include "TestCase.h"

#include <string>
#include <vector>

bool RunFunctionalTests(
    const std::vector<TestCase> &aTestCases, std::vector<TestResult> &aResults,
    std::string &sError);
std::string BuildTextReport(const std::vector<TestResult> &aResults);
std::string BuildJsonReport(const std::vector<TestResult> &aResults);
