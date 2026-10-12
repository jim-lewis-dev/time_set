#include "cli.hpp"
int main(int argc, char** argv) { return ts::cli::run({argv + 1, argv + argc}); }
