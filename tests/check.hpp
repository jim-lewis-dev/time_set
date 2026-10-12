#pragma once
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
namespace test {
inline std::uint64_t checks = 0;
inline void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
}
template<class E = std::exception, class F> void throws(F&& f, int line) {
    ++checks;
    try { f(); }
    catch (const E&) { return; }
    throw std::runtime_error("line " + std::to_string(line) + ": expected exception was not thrown");
}
template<class F> void group(const char* name, F&& function) {
    const auto before = checks;
    function();
    std::cout << name << ": " << checks - before << " checks passed\n";
}
}
#define CHECK(...) ::test::check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)
#define THROWS(...) ::test::throws([&] { (void)(__VA_ARGS__); }, __LINE__)
#define THROWS_AS(type, ...) ::test::throws<type>([&] { (void)(__VA_ARGS__); }, __LINE__)
