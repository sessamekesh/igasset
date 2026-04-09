#ifndef IGASSET_INDEX_BUFFER_H
#define IGASSET_INDEX_BUFFER_H

#include <cstdint>
#include <string>
#include <vector>

namespace igasset {

enum class IndexBufferType {
  Uint16,
  Uint32,
};

class IndexBuffer {
 public:
  class Builder {
   public:
    Builder(IndexBufferType type, size_t size);
    Builder& add(uint16_t index);
    Builder& add(uint32_t index);
    IndexBuffer build();

    Builder() = delete;
    ~Builder() = default;
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    Builder(Builder&&) = default;
    Builder& operator=(Builder&&) = default;

   private:
    std::string buffer_;
    IndexBufferType type_;
    size_t length_;
    size_t filled_length_;
  };

 public:
  IndexBuffer(IndexBufferType type, size_t index_count, std::string raw_buffer);

  IndexBuffer clone() const;

  IndexBufferType type() const {
    return index_buffer_type_;
  }
  const void* data() const {
    return buffer_.data();
  }
  size_t index_count() const {
    return index_count_;
  }
  size_t size() const;

  IndexBuffer() = delete;
  ~IndexBuffer() = default;
  IndexBuffer(const IndexBuffer&) = delete;
  IndexBuffer& operator=(const IndexBuffer&) = delete;
  IndexBuffer(IndexBuffer&&) = default;
  IndexBuffer& operator=(IndexBuffer&&) = default;

 private:
  IndexBufferType index_buffer_type_;
  size_t index_count_;
  std::string buffer_;
};

}  // namespace igasset

#endif
