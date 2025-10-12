#pragma once

constexpr unsigned long long operator""_k(unsigned long long k) {
  return k * 1024;
}

constexpr unsigned long long operator""_m(unsigned long long m) {
  return m * 1024 * 1024;
}

constexpr unsigned long long operator""_g(unsigned long long g) {
  return g * 1024 * 1024 * 1024;
}