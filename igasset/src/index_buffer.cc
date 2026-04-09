#include <igasset/index_buffer.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
size_t index_buffer_byte_width(igasset::IndexBufferType type) {
  switch (type) {
    case igasset::IndexBufferType::Uint16:
      return sizeof(uint16_t);
    case igasset::IndexBufferType::Uint32:
      return sizeof(uint32_t);
  }
  std::unreachable();
}
}  // namespace

namespace igasset {

IndexBuffer::Builder::Builder(IndexBufferType type, size_t size)
    : buffer_(::index_buffer_byte_width(type) * size, '\0'),
      type_(type),
      length_(size),
      filled_length_(0u) {}

IndexBuffer::Builder& IndexBuffer::Builder::add(uint16_t index) {
  if (filled_length_ >= length_) {
    auto log = spdlog::default_logger()->clone("IndexBuffer::Builder");
    log->warn(
        "Attempting to add to an already filled buffer (length={}), ignoring",
        length_);
    return *this;
  }

  if (type_ == IndexBufferType::Uint16) {
    reinterpret_cast<uint16_t*>(buffer_.data())[filled_length_++] = index;
  } else if (type_ == IndexBufferType::Uint32) {
    reinterpret_cast<uint32_t*>(buffer_.data())[filled_length_++] = index;
  }

  return *this;
}

IndexBuffer::Builder& IndexBuffer::Builder::add(uint32_t index) {
  if (filled_length_ >= length_) {
    auto log = spdlog::default_logger()->clone("IndexBuffer::Builder");
    log->warn(
        "Attempting to add to an already filled buffer (length={}), ignoring",
        length_);
    return *this;
  }

  if (type_ == IndexBufferType::Uint16) {
    reinterpret_cast<uint16_t*>(buffer_.data())[filled_length_++] = index;
  } else if (type_ == IndexBufferType::Uint32) {
    reinterpret_cast<uint32_t*>(buffer_.data())[filled_length_++] = index;
  }

  return *this;
}

IndexBuffer IndexBuffer::Builder::build() {
  if (filled_length_ != length_) {
    auto log = spdlog::default_logger()->clone("IndexBuffer::Builder");
    log->warn(
        "Builder has length of {} but was declared with length of {}. "
        "Building, but may produce errors.",
        filled_length_, length_);
  }

  IndexBuffer ib(type_, filled_length_, std::move(buffer_));
  filled_length_ = 0u;
  length_ = 0u;
  return ib;
}

IndexBuffer::IndexBuffer(IndexBufferType type, size_t index_count,
                         std::string raw_buffer)
    : index_buffer_type_(type),
      index_count_(index_count),
      buffer_(std::move(raw_buffer)) {}

IndexBuffer IndexBuffer::clone() const {
  return IndexBuffer(index_buffer_type_, index_count_, buffer_);
}

size_t IndexBuffer::size() const {
  return ::index_buffer_byte_width(index_buffer_type_) * index_count_;
}

}  // namespace igasset
