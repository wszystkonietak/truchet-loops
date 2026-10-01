#pragma once

#include <cassert>
#include <concepts>
#include <span>
#include <type_traits>
#include <utility>

template <typename Policy>
concept JaggedLayoutPolicy = requires(const Policy& policy, size_t row) {
  { policy(row) } -> std::convertible_to<std::pair<size_t, size_t>>;
};

template <typename T, JaggedLayoutPolicy LayoutPolicy>
class jagged_span {
 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using index_type = size_t;
  using pointer = T*;
  using reference = T&;

  constexpr jagged_span(pointer data, LayoutPolicy layout)
      : data_(data), layout_(std::move(layout)) {}

  constexpr reference operator[](index_type row, index_type col) const {
    auto [offset, size] = layout_(row);
    return data_[offset + col];
  }

  constexpr std::span<T> subspan(index_type row) const {
    auto [offset, size] = layout_(row);
    return std::span<T>(data_ + offset, size);
  }

  constexpr pointer data() const noexcept { return data_; }
  constexpr const LayoutPolicy& layout() const noexcept { return layout_; }

 private:
  pointer data_;
  LayoutPolicy layout_;
};

template <typename T, typename LayoutPolicy>
jagged_span(T*, LayoutPolicy) -> jagged_span<T, LayoutPolicy>;