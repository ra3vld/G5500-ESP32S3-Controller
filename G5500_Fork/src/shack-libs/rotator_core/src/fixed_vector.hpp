// G5500 Fork; GPL-3.0.
#pragma once
#include <array>
#include <cstddef>
namespace forkrot {
template<class T, size_t Capacity> class FixedVector {
public:
  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  void clear() { size_ = 0; }
  bool push_back(const T &value) {
    if (size_ == Capacity) return false;
    values_[size_++] = value;
    return true;
  }
  bool assign(const T *values, size_t size) {
    if (size > Capacity) return false;
    for (size_t i = 0; i < size; ++i) values_[i] = values[i];
    size_ = size;
    return true;
  }
  T *data() { return values_.data(); }
  const T *data() const { return values_.data(); }
  T *begin() { return data(); }
  T *end() { return data() + size_; }
  const T *begin() const { return data(); }
  const T *end() const { return data() + size_; }
  T &operator[](size_t i) { return values_[i]; }
  const T &operator[](size_t i) const { return values_[i]; }
private:
  std::array<T, Capacity> values_{};
  size_t size_ = 0;
};
}
