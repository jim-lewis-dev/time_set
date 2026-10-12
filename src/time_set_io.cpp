// Copyright (c) 2026 Jim Lewis. MIT License.
#include "time_set_io.hpp"
#include <array>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <variant>

namespace ts {
namespace {
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
tick_type integer(std::string_view text) {
    text = trim(text);
    if (text.empty()) throw std::invalid_argument("expected a signed 64-bit integer");
    if (text.front() == '+') text.remove_prefix(1);
    tick_type value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec == std::errc::result_out_of_range) throw std::overflow_error("integer exceeds signed 64-bit range");
    if (ec != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("expected an integer: " + std::string(text));
    return value;
}
bool is_integer(std::string_view s) {
    s = trim(s);
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) s.remove_prefix(1);
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
seconds number(std::string_view s) {
    s = trim(s);
    const auto slash = s.find('/');
    if (slash != s.npos) return seconds{integer(s.substr(0, slash)), integer(s.substr(slash + 1))};
    const auto dot = s.find('.');
    if (dot == s.npos) return seconds{integer(s)};
    if (s.find('.', dot + 1) != s.npos) throw std::invalid_argument("invalid decimal duration");
    auto whole = s.substr(0, dot), fraction = s.substr(dot + 1);
    if (fraction.empty() || !std::all_of(fraction.begin(), fraction.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("invalid decimal duration");
    while (!fraction.empty() && fraction.back() == '0') fraction.remove_suffix(1);
    if (fraction.empty()) return seconds{integer(whole.empty() ? "0" : whole)};
    if (fraction.size() > 18) throw std::overflow_error("decimal precision exceeds 18 places; use an exact fraction");
    if (whole.empty() || whole == "+" || whole == "-")
        throw std::invalid_argument("write a leading zero before the decimal point");
    // Preserve a leading minus even for -0.x.
    std::string digits(whole);
    digits.append(fraction);
    tick_type denominator = 1;
    for (std::size_t i = 0; i < fraction.size(); ++i) denominator *= 10;
    return seconds{integer(digits), denominator};
}
seconds multiply_fraction(seconds a, seconds b) {
    auto an = a.numerator(), bn = b.numerator();
    auto ad = a.denominator(), bd = b.denominator();
    auto g = std::gcd(detail::magnitude(an), std::uint64_t(bd));
    an /= static_cast<tick_type>(g); bd /= static_cast<tick_type>(g);
    g = std::gcd(detail::magnitude(bn), std::uint64_t(ad));
    bn /= static_cast<tick_type>(g); ad /= static_cast<tick_type>(g);
    return seconds{detail::multiply(an, bn), detail::multiply(ad, bd)};
}
// Portable 64x64 -> 128 multiplication, followed by division. No compiler
// extensions or floating point. This is used only by calendar formatting.
std::pair<std::uint64_t, std::uint64_t> mul_div(std::uint64_t a, std::uint64_t b, std::uint64_t d) {
    constexpr std::uint64_t mask = 0xffffffffU;
    const auto a0 = a & mask, a1 = a >> 32, b0 = b & mask, b1 = b >> 32;
    auto t = a0 * b0;
    const auto w0 = t & mask;
    auto k = t >> 32;
    t = a1 * b0 + k;
    const auto w1 = t & mask, w2 = t >> 32;
    t = a0 * b1 + w1;
    const auto high = a1 * b1 + w2 + (t >> 32);
    const auto low = (t << 32) + w0;
    if (!d || d > std::uint64_t(max_ticks) || high >= d)
        throw std::overflow_error("calendar duration exceeds 64-bit range");
    auto remainder = high;
    std::uint64_t quotient = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = remainder * 2 + ((low >> bit) & 1U);
        if (remainder >= d) { remainder -= d; quotient |= std::uint64_t{1} << bit; }
    }
    return {quotient, remainder};
}
const auto epoch = std::chrono::sys_days{std::chrono::year{2000}/1/1};

struct json {
    using array = std::vector<json>;
    using object = std::map<std::string, json>;
    std::variant<std::nullptr_t, bool, tick_type, std::string, array, object> value;
    const object& obj() const {
        if (auto p = std::get_if<object>(&value)) return *p;
        throw std::invalid_argument("JSON: expected an object");
    }
    const array& arr() const {
        if (auto p = std::get_if<array>(&value)) return *p;
        throw std::invalid_argument("JSON: expected an array");
    }
    const std::string& str() const {
        if (auto p = std::get_if<std::string>(&value)) return *p;
        throw std::invalid_argument("JSON: expected a string");
    }
    tick_type num() const {
        if (auto p = std::get_if<tick_type>(&value)) return *p;
        throw std::invalid_argument("JSON: expected an integer (not a boolean or decimal)");
    }
};
void validate_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto first = static_cast<unsigned char>(s[i++]);
        if (first < 0x80) continue;
        unsigned n = 0, code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { n = 1; code = first & 31U; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { n = 2; code = first & 15U; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { n = 3; code = first & 7U; minimum = 0x10000; }
        else throw std::invalid_argument("JSON: invalid UTF-8");
        for (unsigned j = 0; j < n; ++j) {
            if (i == s.size()) throw std::invalid_argument("JSON: truncated UTF-8");
            const auto c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0U) != 0x80U) throw std::invalid_argument("JSON: invalid UTF-8 continuation");
            code = (code << 6) | (c & 63U);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            throw std::invalid_argument("JSON: invalid Unicode code point");
    }
}
void utf8_append(std::string& s, unsigned code) {
    if (code <= 0x7f) s.push_back(static_cast<char>(code));
    else if (code <= 0x7ff) {
        s.push_back(static_cast<char>(0xc0U | (code >> 6)));
        s.push_back(static_cast<char>(0x80U | (code & 63U)));
    } else if (code <= 0xffff) {
        s.push_back(static_cast<char>(0xe0U | (code >> 12)));
        s.push_back(static_cast<char>(0x80U | ((code >> 6) & 63U)));
        s.push_back(static_cast<char>(0x80U | (code & 63U)));
    } else {
        s.push_back(static_cast<char>(0xf0U | (code >> 18)));
        s.push_back(static_cast<char>(0x80U | ((code >> 12) & 63U)));
        s.push_back(static_cast<char>(0x80U | ((code >> 6) & 63U)));
        s.push_back(static_cast<char>(0x80U | (code & 63U)));
    }
}
class json_reader {
    std::string_view s_;
    std::size_t p_ = 0;
    [[noreturn]] void fail(std::string_view message) const {
        throw std::invalid_argument("JSON at byte " + std::to_string(p_) + ": " + std::string(message));
    }
    void ws() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_; }
    bool consume(char c) { ws(); if (p_ < s_.size() && s_[p_] == c) { ++p_; return true; } return false; }
    void require(char c) { if (!consume(c)) fail(std::string("expected '") + c + "'"); }
    unsigned hex4() {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            if (p_ == s_.size()) fail("incomplete Unicode escape");
            const char c = s_[p_++];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') digit = unsigned(c - 'a') + 10;
            else if (c >= 'A' && c <= 'F') digit = unsigned(c - 'A') + 10;
            else fail("invalid Unicode escape");
            code = code * 16 + digit;
        }
        return code;
    }
    std::string string() {
        require('"');
        std::string out;
        while (p_ < s_.size()) {
            char c = s_[p_++];
            if (c == '"') { validate_utf8(out); return out; }
            if (static_cast<unsigned char>(c) < 0x20) fail("unescaped control character");
            if (c != '\\') { out.push_back(c); continue; }
            if (p_ == s_.size()) fail("incomplete string escape");
            c = s_[p_++];
            switch (c) {
                case '"': case '\\': case '/': out.push_back(c); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned code = hex4();
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (p_ + 2 > s_.size() || s_[p_] != '\\' || s_[p_+1] != 'u') fail("missing low surrogate");
                        p_ += 2;
                        const auto low = hex4();
                        if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                        code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    } else if (code >= 0xdc00 && code <= 0xdfff) fail("unpaired low surrogate");
                    utf8_append(out, code); break;
                }
                default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }
    json read(unsigned depth) {
        if (depth > 64) fail("nesting is deeper than 64 levels");
        ws();
        if (p_ == s_.size()) fail("unexpected end of input");
        if (s_[p_] == '"') return json{string()};
        if (consume('{')) {
            json::object out;
            if (consume('}')) return json{out};
            do {
                const auto key = string(); require(':');
                auto value = read(depth + 1);
                if (!out.emplace(key, std::move(value)).second) fail("duplicate object key: " + key);
            } while (consume(','));
            require('}'); return json{std::move(out)};
        }
        if (consume('[')) {
            json::array out;
            if (consume(']')) return json{out};
            do { out.push_back(read(depth + 1)); } while (consume(','));
            require(']'); return json{std::move(out)};
        }
        for (auto word : {std::string_view{"null"}, std::string_view{"true"}, std::string_view{"false"}}) {
            if (s_.substr(p_, word.size()) == word) {
                p_ += word.size();
                if (word == "null") return json{nullptr};
                return json{word == "true"};
            }
        }
        const auto start = p_;
        if (s_[p_] == '-') ++p_;
        if (p_ == s_.size() || s_[p_] < '0' || s_[p_] > '9') fail("expected a JSON value");
        if (s_[p_] == '0') ++p_;
        else while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
        if (p_ < s_.size() && (s_[p_] == '.' || s_[p_] == 'e' || s_[p_] == 'E'))
            fail("this schema requires integer numbers, not floating point");
        return json{integer(s_.substr(start, p_ - start))};
    }
public:
    explicit json_reader(std::string_view s) : s_(s) {}
    json parse() { auto out = read(0); ws(); if (p_ != s_.size()) fail("trailing data"); return out; }
};
void keys(const json::object& object, std::initializer_list<std::string_view> required) {
    for (auto key : required)
        if (!object.contains(std::string(key))) throw std::invalid_argument("JSON: missing field '" + std::string(key) + "'");
    for (const auto& [key, unused] : object) {
        (void)unused;
        if (std::find(required.begin(), required.end(), key) == required.end())
            throw std::invalid_argument("JSON: unknown field '" + key + "'");
    }
}
std::string quote(std::string_view s) {
    validate_utf8(s);
    std::ostringstream out;
    out << '"';
    for (const char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
                else out << static_cast<char>(c);
        }
    }
    return out.str() + '"';
}
std::string origin_json(const std::optional<instant>& origin) {
    if (!origin) return "null";
    try { return quote(format_timestamp(*origin)); }
    catch (const std::out_of_range&) {
        return "{\"seconds\":" + std::to_string(origin->whole_seconds()) +
               ",\"fraction\":" + quote(format_duration(origin->fraction())) + "}";
    }
}
time_set decode_set(const json& value) {
    const auto& root = value.obj();
    keys(root, {"timeline", "intervals"});
    const auto& meta = root.at("timeline").obj();
    keys(meta, {"origin", "tick", "length"});
    std::optional<instant> origin;
    const auto& encoded_origin = meta.at("origin");
    if (std::holds_alternative<std::string>(encoded_origin.value)) origin = parse_timestamp(encoded_origin.str());
    else if (std::holds_alternative<json::object>(encoded_origin.value)) {
        const auto& parts = encoded_origin.obj();
        keys(parts, {"seconds", "fraction"});
        origin = instant{parts.at("seconds").num(), parse_duration(parts.at("fraction").str())};
    } else if (!std::holds_alternative<std::nullptr_t>(encoded_origin.value))
        throw std::invalid_argument("JSON: origin must be a civil timestamp, exact origin object, or null");
    timeline line{meta.at("length").num(), parse_duration(meta.at("tick").str()), origin};
    std::vector<interval> intervals;
    for (const auto& item : root.at("intervals").arr()) {
        const auto& pair = item.arr();
        if (pair.size() != 2) throw std::invalid_argument("JSON: each interval needs exactly two integer endpoints");
        intervals.push_back({pair[0].num(), pair[1].num()});
    }
    return time_set{line, std::move(intervals)};
}
} // namespace

seconds parse_duration(std::string_view text) {
    text = trim(text);
    struct unit { std::string_view suffix; tick_type n, d; };
    constexpr std::array units = {unit{"min",60,1}, unit{"ns",1,1000000000},
        unit{"us",1,1000000}, unit{"ms",1,1000}, unit{"s",1,1},
        unit{"h",3600,1}, unit{"d",86400,1}, unit{"w",604800,1}};
    for (const auto& u : units)
        if (text.ends_with(u.suffix)) return multiply_fraction(number(text.substr(0, text.size() - u.suffix.size())), seconds{u.n,u.d});
    throw std::invalid_argument("duration needs a unit: ns, us, ms, s, min, h, d, or w (example: 1/30s)");
}
std::string format_duration(seconds value) {
    auto out = std::to_string(value.numerator());
    if (value.denominator() != 1) out += "/" + std::to_string(value.denominator());
    return out + 's';
}
instant parse_timestamp(std::string_view s) {
    s = trim(s);
    seconds fraction;
    if (const auto marker = s.find("[+"); marker != s.npos) {
        if (!s.ends_with(']')) throw std::invalid_argument("invalid exact fractional timestamp");
        fraction = parse_duration(s.substr(marker + 2, s.size() - marker - 3));
        if (fraction.numerator() < 0 || fraction.numerator() >= fraction.denominator())
            throw std::invalid_argument("timestamp fraction must be in [0,1)");
        s = s.substr(0, marker);
    }
    if (s.size() < 10 || s[4] != '-' || s[7] != '-')
        throw std::invalid_argument("date must be YYYY-MM-DD, optionally followed by T/space and HH:MM[:SS]");
    auto fixed = [&](std::size_t pos, std::size_t length) {
        if (pos + length > s.size()) throw std::invalid_argument("incomplete timestamp");
        const auto text = s.substr(pos,length);
        if (!std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
            throw std::invalid_argument("invalid timestamp digits");
        return static_cast<int>(integer(text));
    };
    const auto year = fixed(0,4), month = fixed(5,2), day = fixed(8,2);
    const auto date = std::chrono::year{year}/std::chrono::month{static_cast<unsigned>(month)}/std::chrono::day{static_cast<unsigned>(day)};
    if (year < 1 || year > 9999 || !date.ok()) throw std::invalid_argument("invalid civil calendar date");
    int hour = 0, minute = 0, second = 0;
    if (s.size() > 10) {
        if (s.size() < 16 || (s[10] != 'T' && s[10] != ' ') || s[13] != ':')
            throw std::invalid_argument("time must be HH:MM or HH:MM:SS; timezone suffixes are not accepted");
        hour = fixed(11,2); minute = fixed(14,2);
        if (s.size() > 16) {
            if (s.size() < 19 || s[16] != ':') throw std::invalid_argument("invalid time; timezone suffixes are not accepted");
            second = fixed(17,2);
            if (s.size() > 19) {
                if (s[19] != '.' || fraction.numerator()) throw std::invalid_argument("invalid fractional time or timezone suffix");
                fraction = number("0" + std::string(s.substr(19)));
            }
        }
    }
    if (hour > 23 || minute > 59 || second > 59) throw std::invalid_argument("invalid civil time (leap seconds are not modeled)");
    const auto days = (std::chrono::sys_days{date} - epoch).count();
    return instant{static_cast<tick_type>(days) * 86400 + hour * 3600 + minute * 60 + second, fraction};
}
std::string format_timestamp(const instant& value) {
    auto days = value.whole_seconds() / 86400;
    auto remainder = value.whole_seconds() % 86400;
    if (remainder < 0) { --days; remainder += 86400; }
    const auto first = (std::chrono::sys_days{std::chrono::year{1}/1/1} - epoch).count();
    const auto last = (std::chrono::sys_days{std::chrono::year{9999}/12/31} - epoch).count();
    if (days < first || days > last) throw std::out_of_range("civil date formatting supports years 0001 through 9999");
    const auto date = std::chrono::year_month_day{epoch + std::chrono::days{days}};
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << int(date.year()) << '-'
        << std::setw(2) << unsigned(date.month()) << '-' << std::setw(2) << unsigned(date.day())
        << 'T' << std::setw(2) << remainder / 3600 << ':' << std::setw(2) << (remainder / 60) % 60
        << ':' << std::setw(2) << remainder % 60;
    if (value.fraction().numerator()) {
        auto denominator = value.fraction().denominator();
        unsigned twos = 0, fives = 0;
        while (denominator % 2 == 0) { denominator /= 2; ++twos; }
        while (denominator % 5 == 0) { denominator /= 5; ++fives; }
        if (denominator == 1 && std::max(twos,fives) <= 18) {
            out << '.';
            auto numerator = std::uint64_t(value.fraction().numerator());
            const auto divisor = std::uint64_t(value.fraction().denominator());
            while (numerator) {
                const auto [digit, rest] = mul_div(numerator, 10, divisor);
                out << digit; numerator = rest;
            }
        } else out << "[+" << format_duration(value.fraction()) << ']';
    }
    return out.str();
}
timeline make_timeline(std::string_view length, std::string_view tick, std::string_view origin) {
    std::optional<instant> start;
    if (origin != "none" && origin != "null") start = parse_timestamp(origin);
    const timeline prototype{0, parse_duration(tick), start};
    return timeline{parse_ticks(prototype,length), prototype.tick_size(), start};
}
tick_type parse_ticks(const timeline& line, std::string_view text) {
    text = trim(text);
    if (is_integer(text)) return integer(text);
    if (text.ends_with('t')) return integer(text.substr(0,text.size()-1));
    return line.to_ticks(parse_duration(text));
}
tick_type parse_position(const timeline& line, std::string_view text) {
    text = trim(text);
    if (text.size() >= 10 && text[4] == '-' && text[7] == '-') {
        if (!line.origin()) throw std::invalid_argument("an indexed timeline has no calendar origin");
        const auto value = parse_timestamp(text);
        auto delta = seconds{detail::subtract(value.whole_seconds(), line.origin()->whole_seconds())};
        delta = delta + (value.fraction() - line.origin()->fraction());
        return line.to_ticks(delta);
    }
    return parse_ticks(line, text);
}
std::string format_position(const timeline& line, tick_type position) {
    if (position < 0 || position > line.length()) throw std::out_of_range("position is outside timeline");
    if (!line.origin()) throw std::invalid_argument("an indexed timeline has no calendar dates");
    const auto [whole, remainder] = mul_div(std::uint64_t(position),
        std::uint64_t(line.tick_size().numerator()), std::uint64_t(line.tick_size().denominator()));
    if (whole > std::uint64_t(max_ticks)) throw std::overflow_error("calendar duration exceeds signed 64-bit range");
    auto seconds_part = detail::add(line.origin()->whole_seconds(), static_cast<tick_type>(whole));
    const seconds fraction{static_cast<tick_type>(remainder), line.tick_size().denominator()};
    const auto& start_fraction = line.origin()->fraction();
    const auto g = std::gcd(fraction.denominator(), start_fraction.denominator());
    const auto denominator = detail::multiply(fraction.denominator() / g, start_fraction.denominator());
    // Each summand is less than denominator; their unsigned sum cannot overflow.
    const auto numerator = std::uint64_t(detail::multiply(fraction.numerator(), start_fraction.denominator()/g)) +
                           std::uint64_t(detail::multiply(start_fraction.numerator(), fraction.denominator()/g));
    seconds_part = detail::add(seconds_part, static_cast<tick_type>(numerator / std::uint64_t(denominator)));
    return format_timestamp(instant{seconds_part,
        seconds{static_cast<tick_type>(numerator % std::uint64_t(denominator)), denominator}});
}
bounds parse_bounds(std::string_view text) {
    if (text == "[)") return bounds::left_closed;
    if (text == "[]") return bounds::closed;
    if (text == "()") return bounds::open;
    if (text == "(]") return bounds::right_closed;
    throw std::invalid_argument("bounds must be [), [], (), or (]");
}
interval parse_region(const timeline& line, std::string_view text) {
    text = trim(text);
    if (text.size() >= 3 && text.front() == '{' && text.back() == '}') {
        const auto p = parse_position(line,text.substr(1,text.size()-2));
        const auto set = line.point(p);
        return set.intervals().front();
    }
    if (text.size() < 5) throw std::invalid_argument("expected an interval such as '[10,20)' or a point '{10}'");
    std::string boundary; boundary += text.front(); boundary += text.back();
    const auto ends = parse_bounds(boundary);
    const auto comma = text.find(',');
    if (comma == text.npos || text.find(',',comma+1) != text.npos) throw std::invalid_argument("interval needs exactly two comma-separated endpoints");
    const auto lo = parse_position(line,text.substr(1,comma-1));
    const auto hi = parse_position(line,text.substr(comma+1,text.size()-comma-2));
    const auto range = line.range(lo,hi,ends);
    if (range.empty()) return {lo,lo};
    return range.intervals().front();
}
time_set parse_text(const timeline& line, std::string_view text) {
    text = trim(text);
    if (text.empty() || text == "empty") return line.empty();
    std::vector<interval> out;
    while (true) {
        const auto split = text.find('|');
        out.push_back(parse_region(line,text.substr(0,split)));
        if (split == text.npos) break;
        text = text.substr(split+1);
        if (trim(text).empty()) throw std::invalid_argument("missing interval after '|'");
    }
    return time_set{line,std::move(out)};
}
std::string to_text(const time_set& set, bool dates) {
    std::ostringstream out;
    if (!dates) { out << set; return out.str(); }
    if (!set.domain().origin()) throw std::invalid_argument("an indexed timeline has no calendar dates");
    if (set.empty()) return "empty";
    bool first = true;
    for (auto iv : set) {
        if (!first) out << " | ";
        first = false;
        out << '[' << format_position(set.domain(),iv.lo) << ',' << format_position(set.domain(),iv.hi) << ')';
    }
    return out.str();
}
std::string to_json(const time_set& set) {
    const auto& line = set.domain();
    std::string out = "{\"timeline\":{\"origin\":" + origin_json(line.origin()) +
        ",\"tick\":" + quote(format_duration(line.tick_size())) +
        ",\"length\":" + std::to_string(line.length()) + "},\"intervals\":[";
    bool first = true;
    for (auto iv : set) {
        if (!first) out += ',';
        first = false;
        out += '[' + std::to_string(iv.lo) + ',' + std::to_string(iv.hi) + ']';
    }
    return out + "]}";
}
time_set from_json(std::string_view text) { return decode_set(json_reader{text}.parse()); }
std::string to_json(const event& value) {
    return "{\"name\":" + quote(value.name) + ",\"description\":" + quote(value.description) +
        ",\"times\":" + to_json(value.times) + "}";
}
event event_from_json(std::string_view text) {
    const auto parsed = json_reader{text}.parse();
    const auto& root = parsed.obj();
    keys(root, {"name","description","times"});
    return {root.at("name").str(), root.at("description").str(), decode_set(root.at("times"))};
}
std::string move_to_json(const time_set& source, const time_set& destination) {
    return "{\"source\":" + to_json(source) + ",\"destination\":" + to_json(destination) + "}";
}
time_set select_from_json(std::string_view text, std::string_view field) {
    if (field != "source" && field != "destination") throw std::invalid_argument("select field must be source or destination");
    const auto parsed = json_reader{text}.parse();
    const auto& object = parsed.obj();
    keys(object,{"source","destination"});
    // Validate both halves, not just the selected half.
    const auto source = decode_set(object.at("source"));
    const auto destination = decode_set(object.at("destination"));
    if (source.domain() != destination.domain()) throw std::invalid_argument("move bundle timelines do not match");
    return field == "source" ? source : destination;
}
std::string statistics_json(const time_set& set) {
    std::string duration;
    bool overflow = false;
    try { duration = quote(format_duration(set.covered_duration())); }
    catch (const std::overflow_error&) { duration = "null"; overflow = true; }
    return "{\"tick_count\":" + std::to_string(set.tick_count()) +
        ",\"interval_count\":" + std::to_string(set.interval_count()) +
        ",\"duration\":" + duration + ",\"duration_overflow\":" + (overflow ? "true" : "false") +
        ",\"empty\":" + (set.empty() ? "true" : "false") + "}";
}
} // namespace ts
