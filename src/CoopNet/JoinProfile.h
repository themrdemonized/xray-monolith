#pragma once
#include "Session.h"
#include <string>
#include <vector>
namespace coopnet {
inline bool normalize_endpoint(const std::string& input,std::string& output) {
    if (input.empty() || input.size()>32) return false;
    const auto colon=input.find(':');
    const auto address=input.substr(0,colon);
    unsigned octets[4]{},index=0,digits=0;
    for (char c:address) {
        if (c=='.') { if (!digits || index==3) return false; ++index; digits=0; }
        else if (c>='0' && c<='9') { if (++digits>3) return false; octets[index]=octets[index]*10+c-'0'; if (octets[index]>255) return false; }
        else return false;
    }
    if (index!=3 || !digits || octets[0]==0 || octets[0]>=224) return false;
    unsigned port=27888;
    if (colon!=std::string::npos) {
        const auto suffix=input.substr(colon+1); if (suffix.empty() || suffix.size()>5) return false;
        port=0; for (char c:suffix) { if (c<'0' || c>'9') return false; port=port*10+c-'0'; }
        if (!port || port>65535) return false;
    }
    output=std::to_string(octets[0])+'.'+std::to_string(octets[1])+'.'+std::to_string(octets[2])+'.'+std::to_string(octets[3])+':'+std::to_string(port);
    return true;
}
struct JoinProfile {
    std::string endpoint;
    Identity character=0;
    BuildIdentity build{};
    Welcome resume{};
};
inline bool valid_join_profile(const JoinProfile& profile) {
    std::string normalized;
    return normalize_endpoint(profile.endpoint,normalized) && normalized==profile.endpoint && profile.character &&
        profile.build.game && profile.build.mods && profile.resume.result==Admission::Accepted &&
        profile.resume.session && profile.resume.player>=2 && profile.resume.resume_token && profile.resume.generation;
}
inline std::vector<std::uint8_t> encode_join_profiles(const std::vector<JoinProfile>& profiles) {
    if (profiles.empty() || profiles.size()>16) throw std::invalid_argument("Invalid saved connections");
    Writer writer; writer.integer(0x434e4a31,4); writer.integer(profiles.size(),1);
    for (const auto& profile:profiles) {
        if (!valid_join_profile(profile)) throw std::invalid_argument("Invalid saved connection");
        writer.integer(profile.endpoint.size(),1); writer.bytes.insert(writer.bytes.end(),profile.endpoint.begin(),profile.endpoint.end());
        writer.integer(profile.character,8); writer.integer(profile.build.game,8); writer.integer(profile.build.mods,8);
        const auto resume=encode_welcome(profile.resume); writer.bytes.insert(writer.bytes.end(),resume.begin(),resume.end());
    }
    return writer.bytes;
}
inline bool decode_join_profiles(const std::vector<std::uint8_t>& bytes,std::vector<JoinProfile>& output) {
    if (bytes.size()>2048) return false;
    Reader reader(bytes); std::uint64_t magic,count;
    if (!reader.integer(magic,4) || magic!=0x434e4a31 || !reader.integer(count,1) || !count || count>16) return false;
    std::vector<JoinProfile> profiles;
    for (unsigned n=0;n<count;++n) {
        JoinProfile profile; std::uint64_t length;
        if (!reader.integer(length,1) || !length || length>32) return false;
        for (unsigned i=0;i<length;++i) { std::uint64_t c; if (!reader.integer(c,1)) return false; profile.endpoint.push_back(static_cast<char>(c)); }
        if (!reader.integer(profile.character,8) || !reader.integer(profile.build.game,8) || !reader.integer(profile.build.mods,8)) return false;
        std::vector<std::uint8_t> resume;
        for (unsigned i=0;i<29;++i) { std::uint64_t c; if (!reader.integer(c,1)) return false; resume.push_back(static_cast<std::uint8_t>(c)); }
        if (!decode_welcome(resume,profile.resume) || !valid_join_profile(profile)) return false;
        for (const auto& existing:profiles) if (existing.endpoint==profile.endpoint) return false;
        profiles.push_back(std::move(profile));
    }
    if (reader.remaining()) return false;
    output=std::move(profiles); return true;
}
}
