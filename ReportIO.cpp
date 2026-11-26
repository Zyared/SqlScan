#include "pch.h"
#include "ReportIO.h"
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#pragma comment(lib, "bcrypt.lib")

namespace io {
std::string Utf8(std::wstring_view s) {
    if (s.empty())
        return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!n)
        throw std::runtime_error("Ошибка преобразования Unicode");
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
static std::wstring Convert(std::string_view s, UINT cp, DWORD flags) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    if (n)
        MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::wstring Wide(std::string_view s) {
    return Convert(s, CP_UTF8, 0);
}
std::string DecodeFile(std::string bytes) {
    if (bytes.starts_with("\xef\xbb\xbf"))
        bytes.erase(0, 3);
    std::replace(bytes.begin(), bytes.end(), '\0', ' ');
    if (!bytes.empty() && Convert(bytes, CP_UTF8, MB_ERR_INVALID_CHARS).empty())
        return Utf8(Convert(bytes, CP_ACP, 0));
    return bytes;
}
std::string Read(const std::filesystem::path &path, std::size_t limit) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    auto size = f.tellg();
    if (size < 0)
        throw std::runtime_error("Не удалось открыть файл");
    if (static_cast<std::uintmax_t>(size) > limit)
        throw std::runtime_error("Превышен лимит размера файла");
    std::string data(static_cast<std::size_t>(size), '\0');
    f.seekg(0);
    if (!f.read(data.data(), size))
        throw std::runtime_error("Ошибка чтения файла");
    return data;
}
void Write(const std::filesystem::path &path, std::string_view data) {
    Write(path, [&](std::ostream &out) { out.write(data.data(), data.size()); });
}
void Write(const std::filesystem::path &path, const std::function<void(std::ostream &)> &write) {
    auto folder = std::filesystem::absolute(path).parent_path();
    wchar_t tmp[MAX_PATH]{};
    if (!GetTempFileNameW(folder.c_str(), L"sqs", 0, tmp))
        throw std::runtime_error("Ошибка создания файла");
    try {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.exceptions(std::ios::badbit | std::ios::failbit);
        write(f);
        f.close();
        if (!MoveFileExW(tmp, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Не удалось заменить файл отчёта");
    } catch (...) {
        DeleteFileW(tmp);
        throw;
    }
}
std::string Hash(std::string_view bytes) {
    BCRYPT_ALG_HANDLE algo{};
    if (BCryptOpenAlgorithmProvider(&algo, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA-256 недоступен");
    unsigned char hash[32];
    auto status = BCryptHash(algo, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())),
                             static_cast<ULONG>(bytes.size()), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(algo, 0);
    if (status < 0)
        throw std::runtime_error("Ошибка SHA-256");
    const char digits[] = "0123456789abcdef";
    std::string hex;
    for (auto b : hash) {
        hex += digits[b >> 4];
        hex += digits[b & 15];
    }
    return hex;
}
std::string Html(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}
std::string Csv(std::string_view s, bool excelSafe) {
    std::string out = "\"";
    auto first = s.find_first_not_of(" \t\r\n");
    if (excelSafe && ((!s.empty() && (s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) ||
                      (first != s.npos && std::string_view("=+-@").find(s[first]) != s.npos)))
        out += '\'';
    for (char c : s) {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + '"';
}
std::string Json(std::string_view s) {
    std::string out = "\"";
    const char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c < 32) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += static_cast<char>(c);
    }
    return out + '"';
}
std::vector<TextSpan> HighRiskSpans(const DetectionReport &r, std::size_t limit) {
    limit = (std::min)(limit, r.normalizedInput.size());
    if (!limit || !r.totalHits)
        return {};
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < limit; ++i)
        if (r.normalizedInput[i] == '\n')
            starts.push_back(i + 1);

    // Bounded by displayed bytes, even if many rules overlap at the same position.
    std::vector<std::size_t> ends(limit);
    r.ForEachHit([&](const DetectionHit &h) {
        if (!h.highRisk || !h.line || h.line > starts.size() || !h.matchedBytes)
            return;
        const auto start = starts[h.line - 1];
        const auto lineEnd = h.line < starts.size() ? starts[h.line] - 1 : limit;
        if (h.position >= lineEnd - start)
            return;
        const auto begin = start + h.position;
        const auto end = begin + (std::min)(h.matchedBytes, lineEnd - begin);
        ends[begin] = (std::max)(ends[begin], end);
    });
    std::vector<TextSpan> spans;
    for (std::size_t i = 0; i < limit; ++i) {
        if (ends[i] == 0)
            continue;
        if (!spans.empty() && i <= spans.back().end)
            spans.back().end = (std::max)(spans.back().end, ends[i]);
        else
            spans.push_back({i, ends[i]});
    }
    return spans;
}

static void WriteCsvReport(std::ostream &out, const DetectionReport &r, std::string_view verdict) {
    out << "verdict,risk,score,threshold,lines,total_hits,saved_hits,truncated\r\n"
        << Csv(verdict) << ',' << Csv(RiskLevel(r)) << ',' << r.totalScore << ',' << r.threshold << ','
        << r.lines.size() << ',' << r.totalHits << ',' << r.totalHits << ',' << r.inputTruncated
        << "\r\n\r\nline,score,suspicious\r\n";
    for (const auto &l : r.lines)
        out << l.line << ',' << l.score << ',' << l.suspicious << "\r\n";
    out << "\r\nline,position,category,pattern,weight,high_risk,context\r\n";
    r.ForEachHit([&](const DetectionHit &h) {
        out << h.line << ',' << h.position << ',' << Csv(h.category) << ',' << Csv(h.pattern) << ','
            << h.weight << ',' << h.highRisk << ',' << Csv(h.context) << "\r\n";
    });
}

void WriteReport(std::ostream &out, const DetectionReport &r, std::string_view format, bool preview) {
    const std::string_view verdict = r.suspicious ? "Подозрительно" : "Чисто";
    if (format == "csv") {
        WriteCsvReport(out, r, verdict);
        return;
    }
    const bool html = format == "html";
    if (html)
        out << "<!doctype html><html lang=\"ru\"><meta charset=\"utf-8\"><title>SqlScan — отчёт</title>"
               "<style>body{max-width:70em;margin:3em auto;font:16px "
               "system-ui;background:#fafafa;color:#19252c}"
               "pre{white-space:pre-wrap}mark{background:#ffd2cc}</style><h1>Отчёт SqlScan</h1><pre>";
    out << verdict << " · риск: " << RiskLevel(r) << "\r\nБаллы: " << r.totalScore
        << "   Порог для строки: " << r.threshold << "   Строк: " << r.lines.size()
        << "   Совпадений: " << r.totalHits << "\r\n";
    out << "Расчёт баллов (число вхождений × вес):\r\n";
    if (r.contributions.empty())
        out << "Совпадений нет. Сумма 0.\r\n";
    std::size_t shown = 0;
    for (const auto &c : r.contributions) {
        if (preview && shown++ == 32) {
            out << "Остальные правила перечислены в полном отчёте.\r\n";
            break;
        }
        out << (html ? Html(c.pattern) : c.pattern) << "  " << c.occurrences << " × "
            << c.weight << " = " << c.occurrences * c.weight << "\r\n";
    }
    auto first = std::find_if(r.lines.begin(), r.lines.end(), [](const LineScore &s) { return s.suspicious; });
    if (first != r.lines.end()) {
        out << "Решение по строке " << first->line << ": ";
        if (first->highRisk)
            out << "есть сигнатура высокого риска";
        if (first->highRisk && first->score >= r.threshold)
            out << ", ";
        if (first->score >= r.threshold)
            out << first->score << " ≥ " << r.threshold;
        out << ".\r\n";
    } else {
        out << "Во всех строках балл ниже порога и нет сигнатур высокого риска.\r\n";
    }
    if (r.inputTruncated)
        out << "Вход ограничен: обработано " << r.analyzedInputBytes << " из " << r.originalInputBytes
            << " байт.\r\n";

    std::size_t count = 0;
    for (const auto &l : r.lines)
        if (!preview || (l.score && count++ < 100))
            out << "Строка " << l.line << " · "
                << (l.suspicious ? "подозрительно"
                    : l.score    ? "ниже порога"
                                 : "чисто")
                << " · " << l.score << " б.\r\n";
    out << "\r\n";
    auto writeHit = [&](const DetectionHit &h) {
        out << h.line << ':' << h.position << "  [" << (html ? Html(h.category) : h.category) << ", +"
            << h.weight << "]  ";
        if (html && h.highRisk)
            out << "<mark>";
        out << (html ? Html(h.pattern) : h.pattern);
        if (html && h.highRisk)
            out << "</mark>";
        out << "   " << (html ? Html(h.context) : h.context) << "\r\n";
    };
    if (preview) {
        for (std::size_t i = 0; i < (std::min)(r.hits.size(), std::size_t(100)); ++i)
            writeHit(r.hits[i]);
        if (r.totalHits > 100)
            out << "Показаны первые 100 совпадений. Полный список доступен в экспорте.\r\n";
    } else {
        r.ForEachHit(writeHit);
    }
    if (!preview) {
        out << "\r\nНормализованный текст\r\n";
        if (html) {
            std::size_t cursor = 0;
            std::string_view input = r.normalizedInput;
            for (auto span : HighRiskSpans(r, input.size())) {
                out << Html(input.substr(cursor, span.begin - cursor)) << "<mark>"
                    << Html(input.substr(span.begin, span.end - span.begin)) << "</mark>";
                cursor = span.end;
            }
            out << Html(input.substr(cursor));
        } else
            out << r.normalizedInput;
    }
    if (html)
        out << "</pre></html>";
}

std::string Report(const DetectionReport &r, std::string_view format, bool preview) {
    std::ostringstream out;
    WriteReport(out, r, format, preview);
    return std::move(out).str();
}
} // namespace io
