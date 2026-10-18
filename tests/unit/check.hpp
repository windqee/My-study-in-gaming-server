#pragma once
#include <cstdlib>
#include <iostream>
inline int checks = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression << '\n'; std::exit(1); } } while (false)
