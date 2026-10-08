#pragma once
#include "Protocol.h"
#include <array>
#include <algorithm>
namespace coopnet {
constexpr std::uint32_t max_baseline_bytes=64*1024*1024;
constexpr std::uint32_t baseline_chunk_bytes=8192;
using BaselineDigest=std::array<std::uint8_t,32>;
struct WorldBaseline {
    Identity id=0;
    std::uint32_t level=0, size=0;
    BaselineDigest digest{};
};
struct WorldChunk {
    Identity baseline=0;
    std::uint32_t offset=0;
    std::vector<std::uint8_t> bytes;
};
inline bool valid_baseline(const WorldBaseline& value) {
    return value.id && value.level && value.size>=12 && value.size<=max_baseline_bytes;
}
inline bool same_baseline(const WorldBaseline& a,const WorldBaseline& b) {
    return a.id==b.id && a.level==b.level && a.size==b.size && a.digest==b.digest;
}
inline std::vector<std::uint8_t> encode_baseline(const WorldBaseline& value) {
    if (!valid_baseline(value)) throw std::invalid_argument("Invalid world baseline");
    Writer writer; writer.integer(value.id,8); writer.integer(value.level,4); writer.integer(value.size,4);
    for (const auto byte:value.digest) writer.integer(byte,1);
    return writer.bytes;
}
inline bool decode_baseline(const std::vector<std::uint8_t>& bytes,WorldBaseline& output) {
    Reader reader(bytes); std::uint64_t id,level,size,byte; WorldBaseline value;
    if (!reader.integer(id,8) || !reader.integer(level,4) || !reader.integer(size,4)) return false;
    value.id=id; value.level=static_cast<std::uint32_t>(level); value.size=static_cast<std::uint32_t>(size);
    for (auto& entry:value.digest) { if (!reader.integer(byte,1)) return false; entry=static_cast<std::uint8_t>(byte); }
    if (reader.remaining() || !valid_baseline(value)) return false;
    output=value; return true;
}
inline bool valid_world_chunk(const WorldChunk& value) {
    return value.baseline && !value.bytes.empty() && value.bytes.size()<=baseline_chunk_bytes &&
        value.offset<max_baseline_bytes && value.bytes.size()<=max_baseline_bytes-value.offset;
}
inline std::vector<std::uint8_t> encode_world_chunk(const WorldChunk& value) {
    if (!valid_world_chunk(value)) throw std::invalid_argument("Invalid world chunk");
    Writer writer; writer.integer(value.baseline,8); writer.integer(value.offset,4); writer.integer(value.bytes.size(),2);
    writer.bytes.insert(writer.bytes.end(),value.bytes.begin(),value.bytes.end()); return writer.bytes;
}
inline bool decode_world_chunk(const std::vector<std::uint8_t>& bytes,WorldChunk& output) {
    Reader reader(bytes); std::uint64_t id,offset,size;
    if (!reader.integer(id,8) || !reader.integer(offset,4) || !reader.integer(size,2) || size!=reader.remaining()) return false;
    WorldChunk value{id,static_cast<std::uint32_t>(offset),{bytes.begin()+14,bytes.end()}};
    if (!valid_world_chunk(value)) return false;
    output=std::move(value); return true;
}
// One bounded assembly per connection. No path or native object ID is supplied by the sender.
class BaselineAssembly {
    WorldBaseline manifest_;
    std::vector<std::uint8_t> bytes_;
public:
    const WorldBaseline& manifest() const { return manifest_; }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    bool complete() const { return manifest_.id && bytes_.size()==manifest_.size; }
    bool begin(const WorldBaseline& value) {
        if (!valid_baseline(value) || manifest_.id) return false;
        manifest_=value; bytes_.reserve(value.size); return true;
    }
    bool append(const WorldChunk& chunk) {
        if (!valid_world_chunk(chunk) || chunk.baseline!=manifest_.id || chunk.offset!=bytes_.size() ||
            chunk.bytes.size()>manifest_.size-bytes_.size()) return false;
        bytes_.insert(bytes_.end(),chunk.bytes.begin(),chunk.bytes.end()); return true;
    }
    void clear() { manifest_={}; std::vector<std::uint8_t>().swap(bytes_); }
};
}
