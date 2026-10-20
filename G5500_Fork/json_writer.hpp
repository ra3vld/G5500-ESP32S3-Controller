// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace forkrot {
struct JsonQuoted { std::string_view text; };
struct JsonEscaped { std::string_view text; };
inline JsonQuoted jsonQuoted(std::string_view text) { return {text}; }
inline JsonEscaped jsonEscaped(std::string_view text) { return {text}; }

// Caller-owned storage: no growing strings, stream buffers, or heap allocations.
class JsonWriter {
public:
  JsonWriter(char *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {
    if (capacity_) buffer_[0] = 0;
    else good_ = false;
  }
  bool good() const { return good_; }
  size_t size() const { return size_; }
  const char *data() const { return buffer_; }
  // Capture one byte page while still counting/hashing the complete document.
  // No full-document allocation, including for RSSI/history/configuration.
  void page(size_t offset) { paging_ = true; offset_ = offset; }
  size_t total() const { return total_; }
  uint32_t fingerprint() const { return hash_; }
  JsonWriter &operator<<(std::string_view text) { return append(text); }
  JsonWriter &operator<<(const char *text) { return append(text); }
  JsonWriter &operator<<(char ch) { return append({&ch, 1}); }
  JsonWriter &operator<<(JsonEscaped value) {
    for (unsigned char ch : value.text) {
      if (ch == '"' || ch == '\\') *this << '\\' << char(ch);
      else if (ch < 32) {
        char code[7];
        std::snprintf(code, sizeof code, "\\u%04x", unsigned(ch));
        *this << code;
      } else *this << char(ch);
    }
    return *this;
  }
  JsonWriter &operator<<(JsonQuoted value) { return *this << '"' << jsonEscaped(value.text) << '"'; }
  template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  JsonWriter &operator<<(T value) {
    char number[32];
    using Number = std::conditional_t<std::is_signed_v<T>, long long, unsigned long long>;
    const auto result = std::to_chars(number, number + sizeof number, static_cast<Number>(value));
    if (result.ec != std::errc()) good_ = false;
    else append({number, size_t(result.ptr - number)});
    return *this;
  }
  JsonWriter &operator<<(float value) { return decimal(value, 2); }
  JsonWriter &operator<<(double value) { return decimal(value, 2); }
  JsonWriter &decimal(double value, int precision) {
    if (!std::isfinite(value)) return *this << "null";
    char number[64];
    // newlib printf's float conversion keeps per-task big-integer allocations.
    // GCC's float/double to_chars uses Ryu with caller-owned output storage.
    const auto result = std::to_chars(number, number + sizeof number - 1, value,
        precision < 0 ? std::chars_format::general : std::chars_format::fixed,
        precision < 0 ? 6 : precision);
    if (result.ec != std::errc()) good_ = false;
    else append({number, size_t(result.ptr - number)});
    return *this;
  }
private:
  JsonWriter &append(std::string_view text) {
    if (!good_) return *this;
    if (text.empty()) return *this;
    for (unsigned char ch : text) hash_ = (hash_ ^ ch) * 16777619u;
    if (paging_) {
      const size_t start = total_ < offset_ ? std::min(text.size(), offset_ - total_) : 0;
      total_ += text.size();
      const size_t count = std::min(text.size() - start, capacity_ - size_ - 1);
      std::memcpy(buffer_ + size_, text.data() + start, count);
      size_ += count;
      buffer_[size_] = 0;
      return *this;
    }
    total_ += text.size();
    if (text.size() >= capacity_ - size_) { good_ = false; return *this; }
    std::memcpy(buffer_ + size_, text.data(), text.size());
    size_ += text.size();
    buffer_[size_] = 0;
    return *this;
  }
  char *buffer_;
  size_t capacity_, size_ = 0;
  size_t offset_ = 0, total_ = 0;
  uint32_t hash_ = 2166136261u;
  bool paging_ = false;
  bool good_ = true;
};
inline constexpr size_t STATUS_JSON_CAPACITY = 6400;
} // namespace forkrot
