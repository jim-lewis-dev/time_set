#include "time_set.hpp"
#include "check.hpp"
#include <bit>
#include <random>
#include <sstream>

using ts::tick_type;
using ts::ticks;
using ts::timeline;
using ts::time_set;
namespace {
using mask_type = std::uint32_t;
time_set from_mask(const timeline& line, mask_type mask) {
    std::vector<ts::interval> out;
    for (tick_type i = 0; i < line.length(); ++i)
        if (mask & (mask_type{1} << i)) out.push_back({i,i+1});
    return time_set{line,std::move(out)};
}
mask_type mask_of(const time_set& value) {
    mask_type out = 0;
    tick_type previous = -1;
    for (auto iv : value) {
        CHECK(iv.lo >= 0 && iv.lo < iv.hi && iv.hi <= value.domain().length());
        CHECK(iv.lo > previous);
        previous = iv.hi;
        for (auto p = iv.lo; p < iv.hi; ++p) out |= mask_type{1} << p;
    }
    CHECK(value.tick_count() == std::popcount(out));
    CHECK(value.empty() == (out == 0));
    CHECK(bool(value) == (out != 0));
    CHECK(!value.contains(-1));
    CHECK(!value.contains(value.domain().length()));
    for (tick_type p = 0; p < value.domain().length(); ++p)
        CHECK(value[p] == bool(out & (mask_type{1} << p)));
    return out;
}
mask_type with_bit(mask_type value, int p, bool bit) {
    const auto mask = mask_type{1} << p;
    return bit ? value | mask : value & ~mask;
}
std::pair<mask_type,mask_type> transfer_oracle(mask_type source, mask_type destination,
    int n, int lo, int hi, int to, bool move, bool overlay, bool same) {
    const auto snapshot = source;
    if (move) for (int p = lo; p < hi; ++p) source = with_bit(source,p,false);
    if (same) destination = source;
    for (int i = 0; i < hi - lo; ++i) {
        const int target = to + i;
        if (target < 0 || target >= n) continue;
        const bool bit = (snapshot & (mask_type{1} << (lo+i))) != 0;
        if (!overlay || bit) destination = with_bit(destination,target,bit);
    }
    if (same) source = destination;
    return {source,destination};
}
void algebra() {
    for (int n = 0; n <= 8; ++n) {
        const timeline line{n};
        const auto limit = mask_type{1} << n;
        const auto universe = limit - 1;
        std::vector<time_set> sets;
        for (mask_type a = 0; a < limit; ++a) {
            sets.push_back(from_mask(line,a));
            CHECK(mask_of(sets.back()) == a);
            CHECK(mask_of(~sets.back()) == (universe ^ a));
            CHECK(~~sets.back() == sets.back());
        }
        for (mask_type a = 0; a < limit; ++a) for (mask_type b = 0; b < limit; ++b) {
            const auto& x = sets[a]; const auto& y = sets[b];
            CHECK(mask_of(x | y) == (a | b));
            CHECK(mask_of(x & y) == (a & b));
            CHECK(mask_of(x - y) == (a & ~b));
            CHECK(mask_of(x ^ y) == (a ^ b));
            CHECK((x == y) == (a == b));
            CHECK((x <= y) == ((a & ~b) == 0));
            CHECK((x < y) == (((a & ~b) == 0) && a != b));
            CHECK((x >= y) == ((b & ~a) == 0));
            CHECK((x > y) == (((b & ~a) == 0) && a != b));
            CHECK(x.overlaps(y) == ((a & b) != 0));
        }
    }
    const timeline line{4};
    for (mask_type a = 0; a < 16; ++a) for (mask_type b = 0; b < 16; ++b) for (mask_type c = 0; c < 16; ++c) {
        const auto x = from_mask(line,a), y = from_mask(line,b), z = from_mask(line,c);
        CHECK((x | (y | z)) == ((x | y) | z));
        CHECK((x & (y & z)) == ((x & y) & z));
        CHECK((x & (y | z)) == ((x & y) | (x & z)));
        CHECK(~(x | y) == (~x & ~y));
        CHECK(~(x & y) == (~x | ~y));
    }
}
void boundaries() {
    const timeline line{8};
    for (int lo = 0; lo <= 8; ++lo) for (int hi = lo; hi <= 8; ++hi) {
        for (auto ends : {ts::bounds::left_closed,ts::bounds::closed,ts::bounds::open,ts::bounds::right_closed}) {
            const bool right_closed = ends == ts::bounds::closed || ends == ts::bounds::right_closed;
            const bool left_open = ends == ts::bounds::open || ends == ts::bounds::right_closed;
            if ((lo != hi && hi == 8 && right_closed) || (lo == hi && lo == 8 && ends == ts::bounds::closed)) {
                THROWS(line.range(lo,hi,ends)); continue;
            }
            mask_type expected = 0;
            for (int p = 0; p < 8; ++p)
                if ((left_open ? p > lo : p >= lo) && (right_closed ? p <= hi : p < hi))
                    expected |= mask_type{1} << p;
            CHECK(mask_of(line.range(lo,hi,ends)) == expected);
        }
    }
    THROWS(line.range(-1,0)); THROWS(line.range(3,2)); THROWS(line.range(0,9));
    THROWS(line.point(8)); THROWS(line.point(-1));
    THROWS(line.range(0,1,static_cast<ts::bounds>(99)));
    CHECK(time_set{line,{{4,6},{1,3},{3,4},{0,0},{5,6},{2,3}}} == line.range(1,6));
    THROWS(time_set(line,{{-1,2}})); THROWS(time_set(line,{{5,4}})); THROWS(time_set(line,{{0,9}}));
    CHECK(timeline{0}.full().empty()); CHECK((~timeline{0}.empty()).empty());
}
void shifts_fits_repeats() {
    const timeline line{8};
    for (mask_type a = 0; a < 256; ++a) {
        const auto value = from_mask(line,a);
        for (int shift = -10; shift <= 10; ++shift) {
            mask_type expected = 0;
            for (int p = 0; p < 8; ++p)
                if ((a & (mask_type{1} << p)) && p + shift >= 0 && p + shift < 8)
                    expected |= mask_type{1} << (p+shift);
            CHECK(mask_of(value + ticks{shift}) == expected);
            CHECK(value + ticks{shift} == (value >> shift));
            CHECK(value - ticks{-shift} == (value >> shift));
            CHECK((value << -shift) == (value >> shift));
        }
        for (int width = 1; width <= 10; ++width) {
            mask_type expected = 0;
            for (int p = 0; p + width <= 8; ++p) {
                bool fits = true;
                for (int q = p; q < p+width; ++q) fits = fits && bool(a & (mask_type{1} << q));
                if (fits) expected |= mask_type{1} << p;
            }
            CHECK(mask_of(value.valid_starts(ticks{width})) == expected);
        }
        for (int step = 1; step <= 10; ++step) for (int count = 0; count <= 10; ++count) {
            mask_type expected = 0;
            for (int k = 0; k < count; ++k) for (int p = 0; p < 8; ++p)
                if ((a & (mask_type{1} << p)) && p + k*step < 8) expected |= mask_type{1} << (p+k*step);
            CHECK(mask_of(value.repeated(ticks{step},count)) == expected);
        }
        for (int step = 1; step <= 4; ++step) for (int until = 0; until <= 8; ++until) {
            mask_type expected = 0;
            for (int k = 0; k < 9; ++k) for (int p = 0; p < 8; ++p)
                if ((a & (mask_type{1} << p)) && p + k*step < until) expected |= mask_type{1} << (p+k*step);
            CHECK(mask_of(value.repeated_until(ticks{step},until)) == expected);
        }
    }
    THROWS(line.empty().valid_starts(ticks{0})); THROWS(line.full().valid_starts(ticks{-1}));
    THROWS(line.empty().repeated(ticks{0},0)); THROWS(line.full().repeated(ticks{1},-1));
    THROWS(line.empty().repeated_until(ticks{1},9));
    CHECK(line.full().valid_starts(ticks{8}) == line.point(0));
}
void editing() {
    constexpr int n = 5;
    const timeline line{n};
    for (mask_type a = 0; a < 32; ++a) for (int lo = 0; lo <= n; ++lo) for (int hi = lo; hi <= n; ++hi) {
        auto filled = from_mask(line,a), cleared = filled;
        mask_type fill_expected = a, clear_expected = a;
        for (int p = lo; p < hi; ++p) { fill_expected = with_bit(fill_expected,p,true); clear_expected = with_bit(clear_expected,p,false); }
        filled.fill(lo,hi); cleared.clear(lo,hi);
        CHECK(mask_of(filled) == fill_expected); CHECK(mask_of(cleared) == clear_expected);
        for (int to = -2; to <= n+2; ++to) for (bool overlay : {false,true}) for (bool moving : {false,true}) {
            const auto mode = overlay ? ts::paste_mode::overlay : ts::paste_mode::replace;
            const auto original = from_mask(line,a);
            auto actual = original;
            const auto expected = transfer_oracle(a,a,n,lo,hi,to,moving,overlay,true).second;
            if (moving) actual.move_region(lo,hi,to,mode,true);
            else actual.copy_region(lo,hi,to,mode,true);
            CHECK(mask_of(actual) == expected);
            actual = original;
            const bool fits = to >= 0 && to <= n && hi-lo <= n-to;
            if (!fits) {
                if (moving) THROWS(actual.move_region(lo,hi,to,mode));
                else THROWS(actual.copy_region(lo,hi,to,mode));
                CHECK(actual == original);
            } else {
                if (moving) actual.move_region(lo,hi,to,mode);
                else actual.copy_region(lo,hi,to,mode);
                CHECK(mask_of(actual) == expected);
            }
        }
    }
    const timeline small{4};
    for (mask_type a = 0; a < 16; ++a) for (mask_type b = 0; b < 16; ++b)
        for (int lo = 0; lo <= 4; ++lo) for (int hi = lo; hi <= 4; ++hi)
            for (int to = -1; to <= 5; ++to) for (bool overlay : {false,true}) for (bool moving : {false,true}) {
                auto source = from_mask(small,a), destination = from_mask(small,b);
                const auto expected = transfer_oracle(a,b,4,lo,hi,to,moving,overlay,false);
                const auto mode = overlay ? ts::paste_mode::overlay : ts::paste_mode::replace;
                if (moving) destination.move_from(source,lo,hi,to,mode,true);
                else destination.copy_from(source,lo,hi,to,mode,true);
                CHECK(mask_of(source) == expected.first);
                CHECK(mask_of(destination) == expected.second);
            }
}
void limits_and_operators() {
    const auto maximum = ts::max_ticks;
    const auto minimum = std::numeric_limits<tick_type>::min();
    const timeline huge{maximum};
    const auto last = huge.point(maximum-1);
    CHECK(last.intervals().front() == ts::interval{maximum-1,maximum});
    CHECK(last == huge.range(maximum-1,maximum-1,ts::bounds::closed));
    CHECK((~last).tick_count() == maximum-1);
    CHECK(huge.full().tick_count() == maximum);
    CHECK((last >> 1).empty()); CHECK((last << -1).empty());
    CHECK((last >> minimum).empty()); CHECK((last << minimum).empty());
    CHECK((huge.full() + ticks{minimum}).empty());
    CHECK((huge.full() - ticks{minimum}).empty());
    CHECK(huge.point(0).repeated(ticks{1},maximum) == huge.full());
    CHECK(huge.full().valid_starts(ticks{maximum}) == huge.point(0));
    CHECK(last.valid_starts(ticks{1}) == last);
    THROWS(huge.range(0,maximum,ts::bounds::closed));
    THROWS(huge.point(maximum));
    auto value = last;
    value.copy_region(maximum-1,maximum,maximum,ts::paste_mode::replace,true);
    CHECK(value == last);
    value.move_region(maximum-1,maximum,minimum,ts::paste_mode::replace,true);
    CHECK(value.empty());
    value = huge.point(0);
    value.move_region(0,1,maximum-1);
    CHECK(value == last);
    CHECK(huge.range(maximum,maximum).empty());
    THROWS_AS(std::overflow_error, ts::timeline(2,ts::seconds{maximum}).full().covered_duration());
    CHECK(ts::seconds(minimum,2).numerator() == minimum/2);
    CHECK(ts::seconds(0,maximum).denominator() == 1);
    THROWS(ts::seconds(1,0)); THROWS(ts::seconds(1,-1));
    THROWS(timeline(-1)); THROWS(timeline(10,ts::seconds{0}));

    const timeline line{100}, different_length{101};
    const timeline other_tick{100,ts::seconds{1,1000}};
    const timeline other_origin{100,ts::seconds{1},ts::instant{1}};
    auto a = line.range(10,20), b = line.range(15,25);
    for (const auto& domain : {different_length,other_tick,other_origin,timeline::indexed(100)}) {
        const auto incompatible = domain.empty();
        CHECK(a != incompatible);
        THROWS(a | incompatible); THROWS(a & incompatible); THROWS(a - incompatible); THROWS(a ^ incompatible);
        THROWS(a <= incompatible); THROWS(a.overlaps(incompatible));
        const auto before = a;
        auto source = incompatible;
        THROWS(a.move_from(source,0,1,0));
        CHECK(a == before); CHECK(source == incompatible);
    }
    auto original_a = a, original_b = b;
    THROWS(a.move_from(b,0,10,99)); CHECK(a == original_a); CHECK(b == original_b);
    THROWS(a.copy_region(0,10,20,static_cast<ts::paste_mode>(42))); CHECK(a == original_a);
    a |= b; CHECK(a == line.range(10,25));
    a &= b; CHECK(a == b);
    a ^= a; CHECK(a.empty());
    a = b; a -= a; CHECK(a.empty());
    a = b; a.copy_from(a,15,25,30); CHECK(a == (b | line.range(30,40)));
    a = b; a.move_from(a,15,25,15); CHECK(a == b);
    a += ticks{5}; a -= ticks{5}; CHECK(a == b);
    a >>= 3; a <<= 3; CHECK(a == b);
    a.assign(0,100,false); CHECK(!a);
    a.assign(0,100,true); CHECK(a == line.full()); a.clear(); CHECK(!a);
    std::ostringstream output; output << a << ' ' << b;
    CHECK(output.str() == "empty [15,25)");

    using namespace std::chrono_literals;
    const timeline frame{100,ts::seconds{1,30}};
    CHECK(frame.to_ticks(100ms) == 3);
    CHECK(frame.to_ticks(ts::seconds{1}) == 30);
    THROWS(frame.to_ticks(50ms));
    CHECK((frame.range(0,1) + 100ms) == frame.range(3,4));
    CHECK((100ms + frame.range(0,1)) == frame.range(3,4));
    CHECK((frame.range(3,4) - 100ms) == frame.range(0,1));
    auto framed = frame.range(3,4); framed -= 100ms; framed += 100ms;
    CHECK(framed == frame.range(3,4));
    const timeline nanos{maximum,ts::seconds{1,1000000000}};
    CHECK(nanos.to_ticks(1s) == 1000000000);
    CHECK(nanos.to_ticks(std::chrono::nanoseconds{minimum}) == minimum);
    THROWS_AS(std::overflow_error,nanos.to_ticks(std::chrono::seconds{maximum}));
    CHECK(timeline(100,ts::seconds{2,60}) == frame);
    static_assert(!ts::duration_value<std::chrono::duration<double>>);
}
void randomized() {
    std::mt19937_64 random{0x74696d655f736574ULL};
    const timeline line{97};
    for (int trial = 0; trial < 2000; ++trial) {
        std::vector<ts::interval> first, second;
        std::vector<bool> a(97), b(97);
        for (int k = 0; k < 30; ++k) {
            auto lo = static_cast<tick_type>(random()%98);
            auto hi = std::min(tick_type{97},lo+static_cast<tick_type>(random()%5));
            (k%2 ? first : second).push_back({lo,hi});
            for (auto i = lo; i < hi; ++i) (k%2 ? a : b)[static_cast<std::size_t>(i)] = true;
        }
        const time_set x{line,first}, y{line,second};
        const auto united = x | y, intersection = x & y, difference = x - y, symmetric = x ^ y;
        for (int i = 0; i < 97; ++i) {
            CHECK(united[i] == (a[static_cast<std::size_t>(i)] || b[static_cast<std::size_t>(i)]));
            CHECK(intersection[i] == (a[static_cast<std::size_t>(i)] && b[static_cast<std::size_t>(i)]));
            CHECK(difference[i] == (a[static_cast<std::size_t>(i)] && !b[static_cast<std::size_t>(i)]));
            CHECK(symmetric[i] == (a[static_cast<std::size_t>(i)] != b[static_cast<std::size_t>(i)]));
        }
    }
}
}
int main() {
    try {
        test::group("exhaustive set algebra (all pairs through 8 ticks)",algebra);
        test::group("all bracket styles and normalization",boundaries);
        test::group("exhaustive shifts, fitting starts and repetitions",shifts_fits_repeats);
        test::group("exhaustive sector edits against snapshot oracle",editing);
        test::group("64-bit limits, aliases, operators, chrono and errors",limits_and_operators);
        test::group("randomized larger timelines",randomized);
        std::cout << "TOTAL: " << test::checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n'; return 1;
    }
}
