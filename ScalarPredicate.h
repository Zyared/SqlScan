#pragma once
#include <cstddef>
#include <string_view>

// Recognize bounded scalar parameter records. A full statement, a clause continuation or
// an arbitrary subquery is not evidence for this matcher. No SQL is ever executed.
namespace scalar_predicate {
struct Evidence {
    std::size_t position{};
    std::size_t length{};
    const char *description{};
};

inline bool Digit(char c) { return c >= '0' && c <= '9'; }
inline bool Letter(char c) { return c >= 'a' && c <= 'z'; }
inline bool NameByte(char c) { return Letter(c) || Digit(c) || c == '_' || c == '$'; }

class Tokens {
  public:
    explicit Tokens(std::string_view line) : text(line) {}
    std::string_view text;

    std::string_view Next() {
        if (text.starts_with(' '))
            text.remove_prefix(1);
        if (text.empty())
            return {};
        std::size_t n = 1;
        if (NameByte(text.front())) {
            while (n < text.size() && NameByte(text[n]))
                ++n;
        } else if (text.starts_with("<>") || text.starts_with("!=") || text.starts_with("<=") ||
                   text.starts_with(">=")) {
            ++n;
        }
        auto token = text.substr(0, n);
        text.remove_prefix(n);
        return token;
    }

    bool Eat(std::string_view expected) {
        auto trial = *this;
        if (trial.Next() != expected)
            return false;
        *this = trial;
        return true;
    }

    bool Integer() {
        auto trial = *this;
        auto token = trial.Next();
        if (token.empty() || token.find_first_not_of("0123456789") != token.npos)
            return false;
        *this = trial;
        return true;
    }

    std::string_view Name() {
        auto trial = *this;
        auto token = trial.Next();
        if (token.empty() || !(Letter(token.front()) || token.front() == '_'))
            return {};
        *this = trial;
        return token;
    }
};

inline bool ConstantCase(Tokens &t) {
    if (!t.Eat("case") || !t.Eat("when"))
        return false;
    unsigned parens = 0;
    while (t.Eat("("))
        ++parens;
    if (!t.Integer())
        return false;
    auto op = t.Next();
    if (op != "=" && op != "<>" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=")
        return false;
    if (!t.Integer())
        return false;
    while (parens > 0) {
        if (!t.Eat(")"))
            return false;
        --parens;
    }
    return t.Eat("then") && t.Integer() && t.Eat("else") && t.Integer() && t.Eat("end");
}

struct Source {
    std::string_view first, second;
    bool operator==(const Source &) const = default;
};

inline Source ReadSource(Tokens &t) {
    Source result{t.Name(), {}};
    if (result.first.empty())
        return {};
    if (t.Eat(".")) {
        result.second = t.Name();
        if (result.second.empty())
            return {};
    }
    if (t.Eat("as")) {
        if (t.Name().empty())
            return {};
    } else {
        t.Name(); // Optional alias. The caller still requires a comma or the closing parenthesis.
    }
    return result;
}

inline bool RepeatedCount(Tokens &t) {
    if (!t.Eat("count") || !t.Eat("(") || !t.Eat("*") || !t.Eat(")") || !t.Eat("from"))
        return false;
    const auto first = ReadSource(t);
    if (first.first.empty())
        return false;
    unsigned count = 1;
    while (t.Eat(",")) {
        if (ReadSource(t) != first)
            return false;
        ++count;
    }
    return count >= 3;
}

inline bool ConditionalProjection(Tokens &t) {
    if (!t.Eat("select"))
        return false;
    unsigned inner = 0;
    while (t.Eat("("))
        ++inner;
    if (!ConstantCase(t))
        return false;
    while (inner > 0) {
        if (!t.Eat(")"))
            return false;
        --inner;
    }
    if (t.Eat("from") && !t.Eat("dual"))
        return false;
    return t.Eat(")");
}

inline Evidence Match(std::string_view line) {
    if (line.empty() || line.size() > 1024)
        return {};
    std::size_t i = 0;
    if (line.front() == '-' || line.front() == '+')
        ++i;
    const auto start = i;
    while (i < line.size() && NameByte(line[i]))
        ++i;
    const auto token = line.substr(start, i - start);
    const bool numeric = !token.empty() && token.find_first_not_of("0123456789") == token.npos;
    bool closing = false;
    while (i < line.size()) {
        const char c = line[i];
        if (c != ' ' && c != '\'' && c != '"' && c != ')')
            break;
        closing |= c != ' ';
        ++i;
    }
    if ((!numeric && !closing) || i > 64)
        return {};
    Tokens t(line.substr(i));
    if (!t.Eat("and") && !t.Eat("or"))
        return {};
    auto count = t;
    while (count.Eat("(")) {}
    if (count.Integer() && count.Eat("=") && count.Eat("(") && count.Eat("select") &&
        RepeatedCount(count) && count.Eat(")")) {
        return {i, line.size() - i - count.text.size(), "Вложенное условие: повтор источника при подсчёте"};
    }

    // Look only within the unquoted operand. A projection inside data or a comment
    // must not become evidence merely because its text resembles SQL.
    while (!t.text.empty()) {
        const auto part = t.Next();
        if (part == "'" || part == "\"" || part == "`" || part == "[" || part == ";" ||
            part.starts_with('$') || (part == "/" && t.text.starts_with('*')) ||
            (part == "-" && t.text.starts_with('-')))
            break;
        if (part == "(") {
            auto projection = t;
            if (ConditionalProjection(projection))
                return {i, line.size() - i - projection.text.size(), "Вложенное условие: константное ветвление"};
        }
    }
    return {};
}
} // namespace scalar_predicate
