#include "time_set_io.hpp"
#include "check.hpp"
#include <random>
using namespace ts;
using namespace std::string_literals;
namespace {
void duration_tests() {
    CHECK(parse_duration("1s") == seconds{1});
    CHECK(parse_duration("250ms") == seconds(1,4));
    CHECK(parse_duration("1/30s") == seconds(1,30));
    CHECK(parse_duration("-0.125s") == seconds(-1,8));
    CHECK(parse_duration("1.5h") == seconds{5400});
    CHECK(parse_duration("2w") == seconds{1209600});
    CHECK(parse_duration("1/2ms") == seconds(1,2000));
    CHECK(parse_duration("1ns") == seconds(1,1000000000));
    CHECK(parse_duration("1us") == seconds(1,1000000));
    CHECK(parse_duration("9223372036854775807.0s") == seconds{max_ticks});
    CHECK(parse_duration("-9223372036854775808s").numerator() == std::numeric_limits<tick_type>::min());
    for (auto text : {"", "1", "1m", "1.2.3s", "1/0s", "1/-3s", "nans", "infs", "1e3s", ".5s", "1.s", "--1s", "1/2/3s"})
        THROWS(parse_duration(text));
    THROWS(parse_duration("9223372036854775808s"));
    THROWS(parse_duration("0.0000000000000000001s"));
    for (auto d : {seconds{},seconds{-4},seconds{1,30},seconds{7,1000000000},seconds{max_ticks,7}})
        CHECK(parse_duration(format_duration(d)) == d);
}
void dates() {
    CHECK(parse_timestamp("2000-01-01").whole_seconds() == 0);
    CHECK(parse_timestamp("1999-12-31T23:59:59").whole_seconds() == -1);
    CHECK(format_timestamp(instant{-1}) == "1999-12-31T23:59:59");
    CHECK(parse_timestamp("2000-02-29").whole_seconds() == 59*86400);
    CHECK(parse_timestamp("2000-03-01").whole_seconds() == 60*86400);
    CHECK(format_timestamp(parse_timestamp("2026-10-11 12:34:56.123456789")) == "2026-10-11T12:34:56.123456789");
    CHECK(format_timestamp(parse_timestamp("2026-10-11T12:34")) == "2026-10-11T12:34:00");
    CHECK(format_timestamp(parse_timestamp("0001-01-01")) == "0001-01-01T00:00:00");
    CHECK(format_timestamp(parse_timestamp("9999-12-31T23:59:59.999999999")) == "9999-12-31T23:59:59.999999999");
    for (auto text : {"1900-02-29", "2100-02-29", "2000-02-30", "2026-13-01", "2026-00-01", "0000-01-01",
                     "2026-10-00", "2026-10-32", "2026-10-11T24:00:00", "2026-10-11T12:60:00",
                     "2026-10-11T12:00:60", "2026-10-11Z", "2026-10-11T12:00:00Z",
                     "2026-10-11T12:00:00-05:00", "2026-10-11T12:00:00+01:00", "2026-10-11 garbage", "26-10-11"})
        THROWS(parse_timestamp(text));
    const auto nanos = make_timeline("10s","1ns","2026-10-11T12:00:00.000000123");
    CHECK(parse_position(nanos,"2026-10-11T12:00:00.000000124") == 1);
    CHECK(format_position(nanos,1) == "2026-10-11T12:00:00.000000124");
    CHECK(parse_position(nanos,format_position(nanos,9999999999LL)) == 9999999999LL);
    const auto frames = make_timeline("100","1/30s","2000-01-01");
    CHECK(format_position(frames,1) == "2000-01-01T00:00:00[+1/30s]");
    CHECK(parse_position(frames,format_position(frames,1)) == 1);
    CHECK(format_position(frames,30) == "2000-01-01T00:00:01");
    for (tick_type i = 0; i <= 100; ++i) CHECK(parse_position(frames,format_position(frames,i)) == i);
    THROWS(parse_position(frames,"2000-01-01T00:00:00.01"));
    const auto fine = make_timeline("20","1/1073741824s"); // finite decimal longer than 18 places
    CHECK(parse_position(fine,format_position(fine,1)) == 1);
    const auto almost_seconds = timeline{100,seconds{max_ticks-1,max_ticks}};
    // A rational duration's numerator would overflow; split calendar arithmetic still works.
    CHECK(format_position(almost_seconds,100).find("00:01:39[+") != std::string::npos);
    const auto unusual_origin = instant{0,seconds{max_ticks-2,max_ticks}};
    const timeline almost{2,seconds{max_ticks-1,max_ticks},unusual_origin};
    CHECK(format_position(almost,1).find("00:00:01[+") != std::string::npos);
    CHECK(make_timeline("100","2/60s","2000-01-01") == frames);
    THROWS(format_position(timeline::indexed(10),0));
    THROWS(parse_position(timeline::indexed(10),"2000-01-01"));
    THROWS(format_position(frames,101)); THROWS(format_position(frames,-1));
    THROWS(format_timestamp(instant{max_ticks}));
}
void json_tests() {
    const auto line = make_timeline("100", "1/30s", "2026-10-11");
    const auto value = line.range(10,20) | line.point(99);
    CHECK(from_json(to_json(value)) == value);
    CHECK(from_json(to_json(~value)) == ~value);
    CHECK(from_json(to_json(line.empty())) == line.empty());
    CHECK(from_json(to_json(timeline::indexed(0).full())) == timeline::indexed(0).full());
    CHECK(from_json(to_json(timeline{max_ticks}.point(max_ticks-1))) == timeline{max_ticks}.point(max_ticks-1));
    CHECK(from_json(to_json(timeline{100,seconds{1},instant{max_ticks}}.full())) == timeline{100,seconds{1},instant{max_ticks}}.full());
    const auto large = timeline{9007199254741000LL}.point(9007199254740993LL);
    CHECK(from_json(to_json(large)) == large);
    CHECK(to_json(large).find("9007199254740993") != std::string::npos);
    const std::string base = R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[1,2],[2,3],[8,8]]})";
    CHECK(from_json(base) == timeline::indexed(10).range(1,3));
    for (auto bad : {
        "", "[]", "null", "{}", "true", "{", "{\"x\":1,}",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[1,2],]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[1,2,3]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[-1,2]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[3,2]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[0,11]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[1.0,2]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[true,2]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[["1",2]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[01,2]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[[+1,2]]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":false})",
        R"({"timeline":{"origin":null,"tick":"0s","length":10},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"-1s","length":10},"intervals":[]})",
        R"({"timeline":{"origin":false,"tick":"1s","length":10},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":-1},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":true},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":1e3},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":9223372036854775808},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10,"extra":0},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[],"extra":0})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10,"length":10},"intervals":[]})",
        R"({"timeline":{"origin":null,"tick":"1s","length":10},"intervals":[],"inter\u0076als":[]})"
    }) THROWS(from_json(bad));
    THROWS(from_json(base + " garbage")); THROWS(from_json(base + base));
    THROWS(from_json(std::string(70,'[') + "0" + std::string(70,']')));
    const event e{"a \"quote\" and \\ slash 🧠", "line\n\ttab\rreturn\bback\fform\0tail"s,value};
    CHECK(event_from_json(to_json(e)) == e);
    const auto encoded_event = R"({"name":"\u0041\ud83e\udde0","description":"\/\b\f\n\r\t","times":)" + to_json(value) + '}';
    CHECK(event_from_json(encoded_event).name == "A🧠");
    for (auto escape : {R"(\ud800)",R"(\udc00)",R"(\ud800\u0041)",R"(\uZZZZ)",R"(\q)"})
        THROWS(event_from_json("{\"name\":\"" + std::string(escape) + "\",\"description\":\"\",\"times\":" + to_json(value) + '}'));
    THROWS(event_from_json(std::string("{\"name\":\"") + '\n' + "\",\"description\":\"\",\"times\":" + to_json(value) + '}'));
    THROWS(event_from_json(std::string("{\"name\":\"") + '\xc0' + '\x80' + "\",\"description\":\"\",\"times\":" + to_json(value) + '}'));
    const auto moved = move_to_json(value,~value);
    CHECK(select_from_json(moved,"source") == value);
    CHECK(select_from_json(moved,"destination") == ~value);
    THROWS(select_from_json(moved,"garbage"));
    THROWS(select_from_json(move_to_json(value,timeline{2}.empty()),"source"));
    CHECK(statistics_json(value).find("\"tick_count\":11") != std::string::npos);
    CHECK(statistics_json(timeline(2,seconds{max_ticks}).full()).find("\"duration_overflow\":true") != std::string::npos);
}
void parser_mutations() {
    std::mt19937_64 random{0x504152534552ULL};
    const auto set = timeline{100}.range(10,20);
    const auto set_document = to_json(set);
    const auto event_document = to_json(event{"Unicode 🧠", "quotes \" and escapes\n", set});
    std::size_t accepted = 0, rejected = 0;
    for (int trial = 0; trial < 20000; ++trial) {
        const bool is_event = (trial % 2) != 0;
        auto mutated = is_event ? event_document : set_document;
        const auto changes = random()%4 + 1;
        for (std::uint64_t k = 0; k < changes; ++k) {
            const auto position = static_cast<std::size_t>(random()%(mutated.size()+1));
            const auto byte = static_cast<char>(static_cast<unsigned char>(random()%256));
            if (random()%3 == 0 && position < mutated.size()) mutated.erase(position,1);
            else if (random()%2 == 0 && position < mutated.size()) mutated[position] = byte;
            else mutated.insert(mutated.begin()+static_cast<std::string::difference_type>(position),byte);
        }
        // A mutation can still be valid. Successful parses must round-trip exactly;
        // rejected inputs must throw documented, catchable validation exceptions.
        try {
            if (is_event) {
                const auto value = event_from_json(mutated);
                CHECK(event_from_json(to_json(value)) == value);
            } else {
                const auto value = from_json(mutated);
                CHECK(from_json(to_json(value)) == value);
            }
            ++accepted;
        } catch (const std::invalid_argument&) { ++rejected; }
          catch (const std::out_of_range&) { ++rejected; }
          catch (const std::overflow_error&) { ++rejected; }
    }
    CHECK(accepted > 0 && rejected > 0);
    CHECK(accepted + rejected == 20000);
    std::cout << "JSON mutation probes: " << accepted << " valid, " << rejected << " rejected\n";
}
void text_and_random() {
    const timeline line{100};
    CHECK(parse_text(line,"[10,20) | [19,30] | {50}") == (line.range(10,31) | line.point(50)));
    CHECK(parse_text(line,"empty").empty()); CHECK(parse_text(line," ").empty());
    CHECK(parse_region(line,"(10,11)") == interval{10,10});
    CHECK(parse_region(line,"[10,10)") == interval{10,10});
    CHECK(parse_region(line,"[10,10]") == interval{10,11});
    CHECK(parse_text(line,"[10,20)") == parse_text(line," (9,19] "));
    THROWS(parse_text(line,"[10,20) |")); THROWS(parse_text(line,"| [10,20)"));
    THROWS(parse_text(line,"[1,2,3)")); THROWS(parse_text(line,"hello"));
    THROWS(parse_text(line,"[1,101)")); THROWS(parse_text(line,"[1,100]"));
    CHECK(parse_ticks(line,"+5") == 5); CHECK(parse_ticks(line,"-5t") == -5);
    CHECK(parse_ticks(line,"2min") == 120);
    THROWS(parse_ticks(line,"0.5s")); THROWS(parse_ticks(line,"0.5t"));
    THROWS(make_timeline("1s","3s"));
    CHECK(make_timeline("10s","1ms").length() == 10000);
    CHECK(make_timeline("10s","1ms").tick_size() == seconds(1,1000));
    std::mt19937_64 random{987654321};
    for (int iteration = 0; iteration < 2000; ++iteration) {
        const auto domain = timeline{1000,seconds{static_cast<tick_type>(random()%100+1),static_cast<tick_type>(random()%100+1)}};
        std::vector<interval> intervals;
        for (int i = 0; i < 20; ++i) {
            auto lo = static_cast<tick_type>(random()%1001), hi = static_cast<tick_type>(random()%1001);
            if (lo > hi) std::swap(lo,hi);
            intervals.push_back({lo,hi});
        }
        const time_set value{domain,intervals};
        CHECK(from_json(to_json(value)) == value);
        CHECK(parse_text(domain,to_text(value)) == value);
    }
}
}
int main() {
    try {
        test::group("exact unit parsing",duration_tests);
        test::group("civil dates, subsecond and fractional formatting",dates);
        test::group("strict JSON, Unicode and 64-bit round trips",json_tests);
        test::group("text and randomized serialization",text_and_random);
        test::group("bounded parser mutation probes",parser_mutations);
        std::cout << "TOTAL: " << test::checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
