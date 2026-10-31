#pragma once

#include "TestCase.h"

#include <string_view>
#include <vector>

std::vector<TestCase> ParseTestCasesJson(std::string_view sInput);
