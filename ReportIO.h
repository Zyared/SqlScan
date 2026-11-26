#pragma once
#include "SqlInjectionDetector.h"
#include <filesystem>
#include <iosfwd>
#include <iosfwd>

namespace io {
std::wstring Wide(std::string_view text);
std::string Utf8(std::wstring_view text);
std::string Read(const std::filesystem::path &file, std::size_t limit);
std::string DecodeFile(std::string bytes);
void Write(const std::filesystem::path &file, std::string_view data);
void Write(const std::filesystem::path &file, const std::function<void(std::ostream &)> &write);
std::string Hash(std::string_view bytes);
std::string Html(std::string_view value);
std::string Csv(std::string_view value, bool excelSafe = true);
std::string Json(std::string_view value);
struct TextSpan {
    std::size_t begin, end;
};
std::vector<TextSpan> HighRiskSpans(const DetectionReport &report, std::size_t byteLimit);
void WriteReport(std::ostream &out, const DetectionReport &report, std::string_view format, bool preview = false);
std::string Report(const DetectionReport &report, std::string_view format, bool preview = false);
} // namespace io
int RunBatch(const std::filesystem::path &input, const std::filesystem::path &output, bool legacy);
