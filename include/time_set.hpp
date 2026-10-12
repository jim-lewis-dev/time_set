// Copyright (c) 2026 Jim Lewis. MIT License.
#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ts {
using tick_type = std::int64_t;
inline constexpr tick_type max_ticks = std::numeric_limits<tick_type>::max();
inline constexpr tick_type default_length = tick_type{1} << 32;

namespace detail {
// Unlike abs(INT64_MIN), this is defined for every signed 64-bit value.
inline std::uint64_t magnitude(tick_type x) noexcept {
    return x < 0 ? std::uint64_t(-(x + 1)) + 1 : std::uint64_t(x);
}
inline tick_type signed_magnitude(std::uint64_t n, bool negative) {
    const auto limit = std::uint64_t(max_ticks) + (negative ? 1U : 0U);
    if (n > limit) throw std::overflow_error("64-bit integer overflow");
    if (!negative) return static_cast<tick_type>(n);
    if (n == limit) return std::numeric_limits<tick_type>::min();
    return -static_cast<tick_type>(n);
}
inline tick_type add(tick_type a, tick_type b) {
    if ((b > 0 && a > max_ticks - b) ||
        (b < 0 && a < std::numeric_limits<tick_type>::min() - b))
        throw std::overflow_error("64-bit integer overflow");
    return a + b;
}
inline tick_type subtract(tick_type a, tick_type b) {
    if ((b > 0 && a < std::numeric_limits<tick_type>::min() + b) ||
        (b < 0 && a > max_ticks + b))
        throw std::overflow_error("64-bit integer overflow");
    return a - b;
}
inline tick_type multiply(tick_type a, tick_type b) {
    const bool negative = (a < 0) != (b < 0);
    const auto x = magnitude(a), y = magnitude(b);
    const auto limit = std::uint64_t(max_ticks) + (negative ? 1U : 0U);
    if (y && x > limit / y) throw std::overflow_error("64-bit integer overflow");
    return signed_magnitude(x * y, negative);
}
// Cancel factors before multiplying: large intermediates cannot silently overflow.
inline tick_type integral_ratio(tick_type value,
                               std::vector<std::uint64_t> numerators,
                               std::vector<std::uint64_t> denominators) {
    if (!value) return 0;
    numerators.push_back(magnitude(value));
    for (auto& d : denominators) {
        if (!d) throw std::invalid_argument("zero denominator");
        for (auto& n : numerators) {
            const auto g = std::gcd(n, d);
            n /= g; d /= g;
        }
        if (d != 1) throw std::invalid_argument("duration is not a whole number of timeline ticks");
    }
    const auto limit = std::uint64_t(max_ticks) + (value < 0 ? 1U : 0U);
    std::uint64_t result = 1;
    for (auto n : numerators) {
        if (n && result > limit / n) throw std::overflow_error("tick count exceeds signed 64-bit range");
        result *= n;
    }
    return signed_magnitude(result, value < 0);
}
} // namespace detail

struct ticks { tick_type count; };

// Exact rational seconds. The denominator is positive; values are normalized.
class seconds {
    tick_type num_ = 0, den_ = 1;
public:
    explicit seconds(tick_type numerator = 0, tick_type denominator = 1) {
        if (denominator <= 0) throw std::invalid_argument("denominator must be positive");
        const auto g = std::gcd(detail::magnitude(numerator), std::uint64_t(denominator));
        num_ = numerator / static_cast<tick_type>(g);
        den_ = denominator / static_cast<tick_type>(g);
    }
    tick_type numerator() const noexcept { return num_; }
    tick_type denominator() const noexcept { return den_; }
    friend bool operator==(const seconds&, const seconds&) = default;
    seconds scaled(tick_type count) const {
        const auto g = std::gcd(detail::magnitude(count), std::uint64_t(den_));
        return seconds{detail::multiply(num_, count / static_cast<tick_type>(g)),
                       den_ / static_cast<tick_type>(g)};
    }
    friend seconds operator+(const seconds& a, const seconds& b) {
        const auto g = std::gcd(a.den_, b.den_);
        return seconds{detail::add(detail::multiply(a.num_, b.den_ / g),
                                   detail::multiply(b.num_, a.den_ / g)),
                       detail::multiply(a.den_ / g, b.den_)};
    }
    friend seconds operator-(const seconds& a, const seconds& b) {
        const auto g = std::gcd(a.den_, b.den_);
        return seconds{detail::subtract(detail::multiply(a.num_, b.den_ / g),
                                        detail::multiply(b.num_, a.den_ / g)),
                       detail::multiply(a.den_ / g, b.den_)};
    }
};

// Calendar-independent identity for an origin: whole seconds since 2000-01-01,
// plus a proper nonnegative fraction. Calendar parsing lives in time_set_io.hpp.
class instant {
    tick_type whole_ = 0;
    seconds fraction_{};
public:
    explicit instant(tick_type whole_seconds = 0, seconds fraction = seconds{})
        : whole_(whole_seconds), fraction_(fraction) {
        if (fraction.numerator() < 0 || fraction.numerator() >= fraction.denominator())
            throw std::invalid_argument("instant fraction must be in [0,1)");
    }
    tick_type whole_seconds() const noexcept { return whole_; }
    const seconds& fraction() const noexcept { return fraction_; }
    friend bool operator==(const instant&, const instant&) = default;
};

enum class bounds { left_closed, closed, open, right_closed };
enum class paste_mode { replace, overlay };
struct interval {
    tick_type lo, hi; // [lo,hi)
    friend bool operator==(const interval&, const interval&) = default;
};
class time_set;

class timeline {
    tick_type length_;
    seconds tick_;
    std::optional<instant> origin_;
public:
    explicit timeline(tick_type length = default_length, seconds tick = seconds{1},
                      std::optional<instant> origin = instant{})
        : length_(length), tick_(tick), origin_(origin) {
        if (length < 0) throw std::invalid_argument("timeline length must be nonnegative");
        if (tick.numerator() <= 0) throw std::invalid_argument("tick size must be positive");
    }
    static timeline indexed(tick_type length, seconds tick = seconds{1}) {
        return timeline{length, tick, std::nullopt};
    }
    tick_type length() const noexcept { return length_; }
    const seconds& tick_size() const noexcept { return tick_; }
    const std::optional<instant>& origin() const noexcept { return origin_; }
    friend bool operator==(const timeline&, const timeline&) = default;
    tick_type to_ticks(ticks t) const noexcept { return t.count; }
    tick_type to_ticks(seconds duration) const {
        return detail::integral_ratio(duration.numerator(),
            {std::uint64_t(tick_.denominator())},
            {std::uint64_t(duration.denominator()), std::uint64_t(tick_.numerator())});
    }
    template<std::integral Rep, class Period>
    tick_type to_ticks(std::chrono::duration<Rep, Period> duration) const {
        if (!std::in_range<tick_type>(duration.count()))
            throw std::overflow_error("duration count exceeds signed 64-bit range");
        return detail::integral_ratio(static_cast<tick_type>(duration.count()),
            {std::uint64_t(Period::num), std::uint64_t(tick_.denominator())},
            {std::uint64_t(Period::den), std::uint64_t(tick_.numerator())});
    }
    time_set empty() const;
    time_set full() const;
    time_set point(tick_type position) const;
    time_set range(tick_type lo, tick_type hi, bounds ends = bounds::left_closed) const;
};

template<class D>
concept duration_value = requires(const timeline& line, D value) {
    { line.to_ticks(value) } -> std::same_as<tick_type>;
};

class time_set {
    timeline line_;
    std::vector<interval> intervals_;

    void compatible(const time_set& other) const {
        if (line_ != other.line_) throw std::invalid_argument("timeline definitions do not match");
    }
    static void append(std::vector<interval>& out, interval part) {
        if (part.lo >= part.hi) return;
        if (!out.empty() && part.lo <= out.back().hi)
            out.back().hi = std::max(out.back().hi, part.hi);
        else out.push_back(part);
    }
    void normalize(std::vector<interval> input) {
        for (auto iv : input)
            if (iv.lo < 0 || iv.hi < iv.lo || iv.hi > line_.length())
                throw std::invalid_argument("interval must satisfy 0 <= start <= end <= timeline length");
        std::sort(input.begin(), input.end(), [](auto a, auto b) {
            return a.lo < b.lo || (a.lo == b.lo && a.hi < b.hi);
        });
        std::vector<interval> out;
        out.reserve(input.size());
        for (auto iv : input) append(out, iv);
        intervals_.swap(out);
    }
    time_set shifted_magnitude(std::uint64_t amount, bool later) const {
        time_set out{line_};
        if (amount >= std::uint64_t(line_.length())) return out;
        const auto n = static_cast<tick_type>(amount);
        for (auto iv : intervals_) {
            if (later) {
                if (iv.lo >= line_.length() - n) break;
                append(out.intervals_, {iv.lo + n,
                    iv.hi > line_.length() - n ? line_.length() : iv.hi + n});
            } else {
                if (iv.hi <= n) continue;
                append(out.intervals_, {iv.lo < n ? 0 : iv.lo - n, iv.hi - n});
            }
        }
        return out;
    }
    time_set shift_signed(tick_type amount, bool reverse = false) const {
        return shifted_magnitude(detail::magnitude(amount), (amount >= 0) != reverse);
    }
    // Clamp base+offset without forming an overflowing signed sum.
    tick_type destination_point(tick_type base, tick_type offset) const noexcept {
        if (base < 0) {
            const auto magnitude = detail::magnitude(base);
            if (magnitude >= std::uint64_t(offset)) return 0;
            return std::min(line_.length(),
                static_cast<tick_type>(std::uint64_t(offset) - magnitude));
        }
        if (base >= line_.length() || offset >= line_.length() - base) return line_.length();
        return base + offset;
    }
    void transfer(time_set* mutable_source, const time_set& source,
                  tick_type lo, tick_type hi, tick_type to,
                  paste_mode mode, bool clip) {
        compatible(source);
        if (mode != paste_mode::replace && mode != paste_mode::overlay)
            throw std::invalid_argument("unknown paste mode");
        const auto sector = line_.range(lo, hi); // validates before any change
        const auto width = hi - lo;
        if (!clip && (to < 0 || to > line_.length() || width > line_.length() - to))
            throw std::out_of_range("destination sector is outside the timeline (enable clipping explicitly)");
        const auto destination = line_.range(destination_point(to, 0), destination_point(to, width));
        const auto snapshot = source & sector;
        time_set pasted{line_};
        for (auto iv : snapshot.intervals_)
            append(pasted.intervals_, {destination_point(to, iv.lo - lo),
                                       destination_point(to, iv.hi - lo)});
        auto result = mutable_source == this ? (*this - sector) : *this;
        if (mode == paste_mode::replace) result -= destination;
        result |= pasted;
        if (mutable_source && mutable_source != this) {
            auto remaining = source - sector;
            mutable_source->intervals_.swap(remaining.intervals_);
        }
        intervals_.swap(result.intervals_); // both commits are noexcept
    }
public:
    explicit time_set(timeline line = timeline{}, std::vector<interval> intervals = {})
        : line_(std::move(line)) { normalize(std::move(intervals)); }

    const timeline& domain() const noexcept { return line_; }
    const std::vector<interval>& intervals() const noexcept { return intervals_; }
    auto begin() const noexcept { return intervals_.begin(); }
    auto end() const noexcept { return intervals_.end(); }
    bool empty() const noexcept { return intervals_.empty(); }
    explicit operator bool() const noexcept { return !empty(); }
    bool operator!() const noexcept { return empty(); }
    std::size_t interval_count() const noexcept { return intervals_.size(); }
    tick_type tick_count() const noexcept {
        tick_type total = 0;
        for (auto iv : intervals_) total += iv.hi - iv.lo; // disjoint; sum <= length
        return total;
    }
    seconds covered_duration() const { return line_.tick_size().scaled(tick_count()); }
    bool contains(tick_type point) const noexcept {
        const auto it = std::lower_bound(intervals_.begin(), intervals_.end(), point,
            [](auto iv, tick_type p) { return iv.hi <= p; });
        return it != intervals_.end() && it->lo <= point;
    }
    bool operator[](tick_type point) const noexcept { return contains(point); }

    time_set united(const time_set& other) const {
        compatible(other);
        time_set out{line_};
        auto a = begin(), b = other.begin();
        while (a != end() || b != other.end()) {
            if (b == other.end() || (a != end() && a->lo <= b->lo)) append(out.intervals_, *a++);
            else append(out.intervals_, *b++);
        }
        return out;
    }
    time_set intersection(const time_set& other) const {
        compatible(other);
        time_set out{line_};
        auto a = begin(), b = other.begin();
        while (a != end() && b != other.end()) {
            append(out.intervals_, {std::max(a->lo, b->lo), std::min(a->hi, b->hi)});
            if (a->hi < b->hi) ++a; else ++b;
        }
        return out;
    }
    time_set difference(const time_set& other) const {
        compatible(other);
        time_set out{line_};
        std::size_t j = 0;
        for (auto a : intervals_) {
            auto cursor = a.lo;
            while (j < other.interval_count() && other.intervals_[j].hi <= cursor) ++j;
            auto k = j;
            while (k < other.interval_count() && other.intervals_[k].lo < a.hi) {
                const auto b = other.intervals_[k];
                append(out.intervals_, {cursor, std::min(b.lo, a.hi)});
                cursor = std::max(cursor, b.hi);
                if (cursor >= a.hi) break;
                ++k;
            }
            append(out.intervals_, {cursor, a.hi});
            j = k;
        }
        return out;
    }
    time_set symmetric_difference(const time_set& other) const {
        return difference(other).united(other.difference(*this));
    }
    time_set complement() const {
        time_set out{line_};
        tick_type cursor = 0;
        for (auto iv : intervals_) {
            append(out.intervals_, {cursor, iv.lo});
            cursor = iv.hi;
        }
        append(out.intervals_, {cursor, line_.length()});
        return out;
    }
    bool is_subset_of(const time_set& other) const {
        compatible(other);
        auto b = other.begin();
        for (auto a : intervals_) {
            while (b != other.end() && b->hi <= a.lo) ++b;
            if (b == other.end() || b->lo > a.lo || b->hi < a.hi) return false;
        }
        return true;
    }
    bool is_proper_subset_of(const time_set& other) const {
        return is_subset_of(other) && *this != other;
    }
    bool overlaps(const time_set& other) const {
        compatible(other);
        auto a = begin(), b = other.begin();
        while (a != end() && b != other.end()) {
            if (std::max(a->lo, b->lo) < std::min(a->hi, b->hi)) return true;
            if (a->hi < b->hi) ++a; else ++b;
        }
        return false;
    }
    friend bool operator==(const time_set&, const time_set&) = default;
    friend bool operator<=(const time_set& a, const time_set& b) { return a.is_subset_of(b); }
    friend bool operator<(const time_set& a, const time_set& b) { return a.is_proper_subset_of(b); }
    friend bool operator>=(const time_set& a, const time_set& b) { return b <= a; }
    friend bool operator>(const time_set& a, const time_set& b) { return b < a; }
    friend time_set operator|(const time_set& a, const time_set& b) { return a.united(b); }
    friend time_set operator&(const time_set& a, const time_set& b) { return a.intersection(b); }
    friend time_set operator-(const time_set& a, const time_set& b) { return a.difference(b); }
    friend time_set operator^(const time_set& a, const time_set& b) { return a.symmetric_difference(b); }
    time_set operator~() const { return complement(); }
    time_set& operator|=(const time_set& b) { return *this = *this | b; }
    time_set& operator&=(const time_set& b) { return *this = *this & b; }
    time_set& operator-=(const time_set& b) { return *this = *this - b; }
    time_set& operator^=(const time_set& b) { return *this = *this ^ b; }

    template<duration_value D> time_set shifted(D amount) const { return shift_signed(line_.to_ticks(amount)); }
    template<duration_value D> friend time_set operator+(const time_set& a, D d) { return a.shifted(d); }
    template<duration_value D> friend time_set operator+(D d, const time_set& a) { return a.shifted(d); }
    template<duration_value D> friend time_set operator-(const time_set& a, D d) {
        return a.shift_signed(a.line_.to_ticks(d), true);
    }
    template<duration_value D> time_set& operator+=(D d) { return *this = *this + d; }
    template<duration_value D> time_set& operator-=(D d) { return *this = *this - d; }
    time_set operator>>(tick_type n) const { return shift_signed(n); }
    time_set operator<<(tick_type n) const { return shift_signed(n, true); }
    time_set& operator>>=(tick_type n) { return *this = *this >> n; }
    time_set& operator<<=(tick_type n) { return *this = *this << n; }

    time_set& fill(tick_type lo, tick_type hi, bounds ends = bounds::left_closed) {
        return *this |= line_.range(lo, hi, ends);
    }
    time_set& clear(tick_type lo, tick_type hi, bounds ends = bounds::left_closed) {
        return *this -= line_.range(lo, hi, ends);
    }
    time_set& clear() noexcept { intervals_.clear(); return *this; }
    time_set& assign(tick_type lo, tick_type hi, bool value, bounds ends = bounds::left_closed) {
        return value ? fill(lo, hi, ends) : clear(lo, hi, ends);
    }
    time_set& copy_region(tick_type lo, tick_type hi, tick_type to,
                          paste_mode mode = paste_mode::replace, bool clip = false) {
        transfer(nullptr, *this, lo, hi, to, mode, clip); return *this;
    }
    time_set& move_region(tick_type lo, tick_type hi, tick_type to,
                          paste_mode mode = paste_mode::replace, bool clip = false) {
        transfer(this, *this, lo, hi, to, mode, clip); return *this;
    }
    time_set& copy_from(const time_set& source, tick_type lo, tick_type hi, tick_type to,
                        paste_mode mode = paste_mode::replace, bool clip = false) {
        transfer(nullptr, source, lo, hi, to, mode, clip); return *this;
    }
    time_set& move_from(time_set& source, tick_type lo, tick_type hi, tick_type to,
                        paste_mode mode = paste_mode::replace, bool clip = false) {
        transfer(&source, source, lo, hi, to, mode, clip); return *this;
    }

    template<duration_value D> time_set valid_starts(D duration) const {
        const auto width = line_.to_ticks(duration);
        if (width <= 0) throw std::invalid_argument("activity duration must be positive");
        time_set out{line_};
        for (auto iv : intervals_)
            if (iv.hi - iv.lo >= width) append(out.intervals_, {iv.lo, iv.hi - width + 1});
        return out;
    }
    template<duration_value D> time_set repeated(D every, tick_type count) const {
        const auto step = line_.to_ticks(every);
        if (step <= 0) throw std::invalid_argument("repeat period must be positive");
        if (count < 0) throw std::invalid_argument("repeat count must be nonnegative");
        if (empty() || !count) return line_.empty();
        count = std::min(count, (line_.length() - 1 - intervals_.front().lo) / step + 1);
        time_set result{line_}, block = *this;
        tick_type offset = 0, stride = std::min(step, line_.length());
        // Binary doubling avoids looping billions of times for dense patterns.
        while (count) {
            if (count & 1) {
                result |= block >> offset;
                offset = stride > line_.length() - offset ? line_.length() : offset + stride;
            }
            count >>= 1;
            if (!count) break;
            block |= block >> stride;
            stride = stride > line_.length() - stride ? line_.length() : stride + stride;
        }
        return result;
    }
    template<duration_value D> time_set repeated_until(D every, tick_type until) const {
        const auto step = line_.to_ticks(every);
        if (step <= 0) throw std::invalid_argument("repeat period must be positive");
        if (until < 0 || until > line_.length()) throw std::out_of_range("repeat boundary is outside timeline");
        if (empty() || intervals_.front().lo >= until) return line_.empty();
        const auto count = (until - 1 - intervals_.front().lo) / step + 1;
        return repeated(ticks{step}, count) & line_.range(0, until);
    }
    friend std::ostream& operator<<(std::ostream& stream, const time_set& set) {
        if (set.empty()) return stream << "empty";
        bool first = true;
        for (auto iv : set.intervals_) {
            if (!first) stream << " | ";
            first = false;
            stream << '[' << iv.lo << ',' << iv.hi << ')';
        }
        return stream;
    }
};

inline time_set timeline::empty() const { return time_set{*this}; }
inline time_set timeline::full() const { return range(0, length_); }
inline time_set timeline::point(tick_type position) const {
    if (position < 0 || position >= length_) throw std::out_of_range("point is outside timeline");
    return time_set{*this, {{position, position + 1}}};
}
inline time_set timeline::range(tick_type lo, tick_type hi, bounds ends) const {
    if (ends != bounds::left_closed && ends != bounds::closed &&
        ends != bounds::open && ends != bounds::right_closed)
        throw std::invalid_argument("unknown interval boundary style");
    if (lo < 0 || hi < lo || hi > length_)
        throw std::out_of_range("interval must satisfy 0 <= start <= end <= timeline length");
    if (lo == hi) return ends == bounds::closed ? point(lo) : empty();
    if (ends == bounds::closed || ends == bounds::right_closed) {
        if (hi == length_) throw std::out_of_range("closed endpoint must be an actual tick, less than timeline length");
        ++hi;
    }
    if (ends == bounds::open || ends == bounds::right_closed) ++lo;
    return time_set{*this, {{lo, hi}}};
}
} // namespace ts
