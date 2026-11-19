#include "pch.h"
#include "ReportIO.h"
#include <algorithm>
#include <stdexcept>

namespace {
bool NextRow(std::string_view csv, std::size_t &pos, std::vector<std::string> &row) {
    row.clear();
    if (pos >= csv.size())
        return false;
    for (;;) {
        std::string field;
        if (pos < csv.size() && csv[pos] == '"') {
            ++pos;
            bool closed = false;
            while (pos < csv.size()) {
                char c = csv[pos++];
                if (c != '"') {
                    field += c;
                    continue;
                }
                if (pos < csv.size() && csv[pos] == '"') {
                    field += '"';
                    ++pos;
                    continue;
                }
                closed = true;
                break;
            }
            if (!closed)
                throw std::runtime_error("Незакрытые кавычки в CSV");
        } else {
            auto end = csv.find_first_of(",\r\n", pos);
            field = csv.substr(pos, end - pos);
            if (field.find('"') != field.npos)
                throw std::runtime_error("Лишние кавычки в CSV");
            pos = end == csv.npos ? csv.size() : end;
        }
        row.push_back(std::move(field));
        if (pos == csv.size())
            return true;
        char sep = csv[pos++];
        if (sep == ',')
            continue;
        if (sep == '\r' && pos < csv.size() && csv[pos] == '\n')
            ++pos;
        else if (sep != '\r' && sep != '\n')
            throw std::runtime_error("Некорректный разделитель CSV");
        return true;
    }
}
} // namespace
int RunBatch(const std::filesystem::path &input, const std::filesystem::path &output, bool legacy) {
    if (std::filesystem::exists(output) && std::filesystem::equivalent(input, output))
        throw std::runtime_error("Входной и выходной файлы совпадают");
    auto data = io::DecodeFile(io::Read(input, 64 * 1024 * 1024));
    std::vector<std::string> row;
    std::size_t pos = 0;
    if (!NextRow(data, pos, row))
        throw std::runtime_error("Пустой CSV");
    auto text = std::find(row.begin(), row.end(), "text");
    if (std::count(row.begin(), row.end(), "text") > 1 || std::count(row.begin(), row.end(), "label") > 1)
        throw std::runtime_error("Неоднозначный заголовок CSV: text и label допускаются один раз");
    if (text == row.end())
        throw std::runtime_error("Обязательный столбец CSV — text");
    auto col = text - row.begin(), label = std::find(row.begin(), row.end(), "label") - row.begin();
    auto columns = row.size();
    SqlInjectionDetector detector;
    std::string result = "row,label,predicted,score,risk,hits,truncated\r\n";
    std::size_t index = 0;
    while (NextRow(data, pos, row)) {
        if (row.size() != columns)
            throw std::runtime_error("Неверное число полей CSV в записи " + std::to_string(index + 1));
        auto r = detector.Analyze(row[col], legacy);
        result += std::to_string(++index) + "," +
                  io::Csv(static_cast<std::size_t>(label) < columns ? row[label] : "") + "," +
                  (r.suspicious ? "1" : "0") + "," + std::to_string(r.totalScore) + "," +
                  io::Csv(RiskLevel(r)) + "," + std::to_string(r.totalHits) + "," +
                  (r.inputTruncated ? "1" : "0") + "\r\n";
    }
    io::Write(output, "\xef\xbb\xbf" + result);
    return 0;
}
