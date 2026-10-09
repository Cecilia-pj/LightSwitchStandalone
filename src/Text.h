// LightSwitch Standalone - small text helpers (UTF-8 <-> UTF-16).
#pragma once

#include <string>

namespace ls
{

std::string WideToUtf8(const std::wstring& wide);
std::wstring Utf8ToWide(const std::string& utf8);

// Case-insensitive ASCII comparison for JSON keys / mode strings.
bool EqualsIgnoreCaseAscii(const std::string& a, const char* b);

} // namespace ls
