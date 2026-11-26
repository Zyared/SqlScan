#include "pch.h"
#include "ReportIO.h"
#include <ostream>
#include <stdexcept>

namespace {
// Views refer to the decoded input. Doubled quotes are unescaped only for columns we use.
std::string_view ReadField(std::string_view csv, std::size_t &pos) {
    if (pos == csv.size() || csv[pos] != '"') {
        const auto end = csv.find_first_of(",\r\n", pos);
        const auto field = csv.substr(pos, end - pos);
        if (field.find('"') != field.npos)
            throw std::runtime_error("Лишние кавычки в CSV");
        pos = end == csv.npos ? csv.size() : end;
        return field;
    }
    const auto begin = ++pos;
    for (;;) {
        const auto quote = csv.find('"', pos);
        if (quote == csv.npos)
            throw std::runtime_error("Незакрытые кавычки в CSV");
        pos = quote + 1;
        if (pos == csv.size() || csv[pos] != '"')
            return csv.substr(begin, quote - begin);
        ++pos;
    }
}

template <typename Visitor>
std::size_t ReadRow(std::string_view csv, std::size_t &pos, Visitor visit) {
    if (pos == csv.size())
        return 0;
    std::size_t count = 0;
    for (;;) {
        visit(count++, ReadField(csv, pos));
        if (pos == csv.size())
            return count;
        const char separator = csv[pos++];
        if (separator == ',')
            continue;
        if (separator != '\r' && separator != '\n')
            throw std::runtime_error("Некорректный разделитель CSV");
        if (separator == '\r' && pos < csv.size() && csv[pos] == '\n')
            ++pos;
        return count;
    }
}

std::string_view UnescapeField(std::string_view field, std::string &scratch) {
    auto quote = field.find('"');
    if (quote == field.npos)
        return field;
    scratch.clear();
    do {
        scratch.append(field.substr(0, quote + 1));
        field.remove_prefix(quote + 2);
        quote = field.find('"');
    } while (quote != field.npos);
    scratch.append(field);
    return scratch;
}

struct Columns {
    std::size_t text = std::string_view::npos;
    std::size_t label = std::string_view::npos;
    std::size_t count = 0;
};

Columns ReadHeader(std::string_view data, std::size_t &pos) {
    Columns header;
    header.count = ReadRow(data, pos, [&](std::size_t index, std::string_view name) {
        if (name != "text" && name != "label")
            return;
        auto &column = name == "text" ? header.text : header.label;
        if (column != std::string_view::npos)
            throw std::runtime_error("Неоднозначный заголовок CSV: text и label допускаются один раз");
        column = index;
    });
    if (!header.count)
        throw std::runtime_error("Пустой CSV");
    if (header.text == std::string_view::npos)
        throw std::runtime_error("Обязательный столбец CSV — text");
    return header;
}
} // namespace

int RunBatch(const std::filesystem::path &input, const std::filesystem::path &output, bool legacy) {
    if (std::filesystem::exists(output) && std::filesystem::equivalent(input, output))
        throw std::runtime_error("Входной и выходной файлы совпадают");
    const auto data = io::DecodeFile(io::Read(input, 64 * 1024 * 1024));
    std::size_t pos = 0;
    const auto columns = ReadHeader(data, pos);
    const SqlInjectionDetector detector;
    io::Write(output, [&](std::ostream &out) {
        out << "\xef\xbb\xbfrow,label,predicted,score,risk,hits,truncated\r\n";
        std::string textScratch, labelScratch;
        for (std::size_t number = 1; pos < data.size(); ++number) {
            std::string_view text, label;
            const auto count = ReadRow(data, pos, [&](std::size_t index, std::string_view field) {
                if (index == columns.text)
                    text = UnescapeField(field, textScratch);
                else if (index == columns.label)
                    label = UnescapeField(field, labelScratch);
            });
            if (count != columns.count)
                throw std::runtime_error("Неверное число полей CSV в записи " + std::to_string(number));
            const auto report = detector.Analyze(text, legacy);
            out << number << ',' << io::Csv(label) << ',' << (report.suspicious ? '1' : '0') << ','
                << report.totalScore << ',' << io::Csv(RiskLevel(report)) << ',' << report.totalHits << ','
                << (report.inputTruncated ? '1' : '0') << "\r\n";
        }
    });
    return 0;
}
