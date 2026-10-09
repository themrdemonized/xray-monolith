#pragma once
#include "SharedWorld.h"
namespace coopnet {
enum class DialogueAction : std::uint8_t { Open=1, Select, Close };
struct DialogueRequest {
    Identity actor=0,target=0,incarnation=0;
    std::uint32_t generation=0,level=0,sequence=0,revision=0;
    DialogueAction action=DialogueAction::Open;
    std::string dialog,phrase;
};
inline bool valid_dialogue_request(const DialogueRequest& r) {
    if (!r.actor || !r.target || !r.incarnation || !r.generation || !r.level || !r.sequence) return false;
    if (r.action==DialogueAction::Open) return !r.revision && r.dialog.empty() && r.phrase.empty();
    if (r.action==DialogueAction::Close) return r.revision && r.dialog.empty() && r.phrase.empty();
    return r.action==DialogueAction::Select && r.revision && shared_name(r.dialog,128) && (r.phrase.empty() || shared_name(r.phrase,128));
}
inline std::vector<std::uint8_t> encode_dialogue_request(const DialogueRequest& r) {
    if (!valid_dialogue_request(r)) throw std::invalid_argument("Dialogue request");
    SharedWriter w; for (auto n:{r.actor,r.target,r.incarnation}) w.integer(n,8);
    for (auto n:{r.generation,r.level,r.sequence,r.revision}) w.integer(n,4);
    w.integer(static_cast<unsigned>(r.action),1); w.string(r.dialog); w.string(r.phrase); return w.bytes;
}
inline bool decode_dialogue_request(const std::vector<std::uint8_t>& bytes,DialogueRequest& output) {
    Reader reader(bytes); DialogueRequest r; std::uint64_t n;
    for (auto* value:{&r.actor,&r.target,&r.incarnation}) if (!reader.integer(*value,8)) return false;
    for (auto* value:{&r.generation,&r.level,&r.sequence,&r.revision}) { if (!reader.integer(n,4)) return false; *value=static_cast<std::uint32_t>(n); }
    if (!reader.integer(n,1)) return false; r.action=static_cast<DialogueAction>(n);
    if (!shared_string(reader,r.dialog,128) || !shared_string(reader,r.phrase,128) || reader.remaining() || !valid_dialogue_request(r)) return false;
    output=std::move(r); return true;
}
struct DialogueChoice { std::string dialog,phrase,text; };
struct DialogueView {
    Identity actor=0,target=0,incarnation=0;
    std::uint32_t generation=0,level=0,revision=0;
    bool finished=false;
    std::vector<DialogueChoice> choices;
};
inline bool valid_dialogue_view(const DialogueView& v) {
    if (!v.actor || !v.target || !v.incarnation || !v.generation || !v.level || !v.revision || v.choices.size()>256 || (v.finished && !v.choices.empty())) return false;
    std::set<std::pair<std::string,std::string>> keys;
    std::size_t encoded_size=39;
    for (const auto& c:v.choices) {
        if (!shared_name(c.dialog,128) || (!c.phrase.empty() && !shared_name(c.phrase,128)) || c.text.size()>4096 || c.text.find('\0')!=std::string::npos || !keys.insert({c.dialog,c.phrase}).second) return false;
        encoded_size+=6+c.dialog.size()+c.phrase.size()+c.text.size();
        if (encoded_size>shared_limit) return false;
    }
    return true;
}
inline std::vector<std::uint8_t> encode_dialogue_view(const DialogueView& v) {
    if (!valid_dialogue_view(v)) throw std::invalid_argument("Dialogue view");
    SharedWriter w; for (auto n:{v.actor,v.target,v.incarnation}) w.integer(n,8);
    for (auto n:{v.generation,v.level,v.revision}) w.integer(n,4);
    w.integer(v.finished,1); w.integer(v.choices.size(),2);
    for (const auto& c:v.choices) { w.string(c.dialog); w.string(c.phrase); w.string(c.text); } return w.bytes;
}
inline bool decode_dialogue_view(const std::vector<std::uint8_t>& bytes,DialogueView& output) {
    if (bytes.size()>shared_limit) return false;
    Reader reader(bytes); DialogueView v; std::uint64_t n,count;
    for (auto* value:{&v.actor,&v.target,&v.incarnation}) if (!reader.integer(*value,8)) return false;
    for (auto* value:{&v.generation,&v.level,&v.revision}) { if (!reader.integer(n,4)) return false; *value=static_cast<std::uint32_t>(n); }
    if (!reader.integer(n,1) || n>1 || !reader.integer(count,2) || count>256) return false; v.finished=n!=0;
    for (unsigned i=0;i<count;++i) { DialogueChoice c; if (!shared_string(reader,c.dialog,128) || !shared_string(reader,c.phrase,128) || !shared_string(reader,c.text,4096)) return false; v.choices.push_back(std::move(c)); }
    if (reader.remaining() || !valid_dialogue_view(v)) return false; output=std::move(v); return true;
}
// This checks the offered revision only. Native distance, liveness and Lua
// preconditions must be rechecked on the host immediately before executing it.
inline bool offered_dialogue_choice(const DialogueView& v,const DialogueRequest& r) {
    if (!valid_dialogue_view(v) || !valid_dialogue_request(r) || v.finished || r.action!=DialogueAction::Select ||
        r.actor!=v.actor || r.target!=v.target || r.incarnation!=v.incarnation || r.generation!=v.generation || r.level!=v.level || r.revision!=v.revision) return false;
    for (const auto& c:v.choices) if (c.dialog==r.dialog && c.phrase==r.phrase) return true;
    return false;
}
struct DialogueChunk {
    Identity actor=0;
    std::uint32_t generation=0,level=0,revision=0,sequence=0,total=0,offset=0;
    std::vector<std::uint8_t> bytes;
};
inline bool valid_dialogue_chunk(const DialogueChunk& c) {
    return c.actor && c.generation && c.level && c.revision && c.sequence && c.total && c.total<=shared_limit &&
        c.offset<c.total && c.offset%8192==0 && c.bytes.size()==(std::min)(std::size_t(8192),std::size_t(c.total-c.offset));
}
inline std::vector<std::uint8_t> encode_dialogue_chunk(const DialogueChunk& c) {
    if (!valid_dialogue_chunk(c)) throw std::invalid_argument("Dialogue chunk");
    Writer w; w.integer(c.actor,8); for (auto n:{c.generation,c.level,c.revision,c.sequence,c.total,c.offset}) w.integer(n,4);
    w.bytes.insert(w.bytes.end(),c.bytes.begin(),c.bytes.end()); return w.bytes;
}
inline bool decode_dialogue_chunk(const std::vector<std::uint8_t>& bytes,DialogueChunk& output) {
    if (bytes.size()>max_payload) return false;
    Reader reader(bytes); DialogueChunk c; std::uint64_t n;
    if (!reader.integer(c.actor,8)) return false;
    for (auto* value:{&c.generation,&c.level,&c.revision,&c.sequence,&c.total,&c.offset}) { if (!reader.integer(n,4)) return false; *value=static_cast<std::uint32_t>(n); }
    c.bytes.assign(bytes.end()-reader.remaining(),bytes.end()); if (!valid_dialogue_chunk(c)) return false;
    output=std::move(c); return true;
}
class DialogueAssembly {
    DialogueChunk header_;
    std::vector<std::uint8_t> bytes_;
public:
    void clear() { header_={}; bytes_.clear(); }
    bool accept(const DialogueChunk& c) {
        if (!valid_dialogue_chunk(c)) return false;
        if (!c.offset) { header_=c; bytes_.clear(); bytes_.reserve(c.total); }
        if (c.actor!=header_.actor || c.generation!=header_.generation || c.level!=header_.level || c.revision!=header_.revision ||
            c.sequence!=header_.sequence || c.total!=header_.total || c.offset!=bytes_.size()) return false;
        bytes_.insert(bytes_.end(),c.bytes.begin(),c.bytes.end()); return true;
    }
    bool complete() const { return header_.total && bytes_.size()==header_.total; }
    bool view(DialogueView& output) const {
        DialogueView v;
        if (!complete() || !decode_dialogue_view(bytes_,v) || v.actor!=header_.actor || v.generation!=header_.generation || v.level!=header_.level || v.revision!=header_.revision) return false;
        output=std::move(v); return true;
    }
};
}
