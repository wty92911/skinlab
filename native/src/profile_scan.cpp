// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include <skinlab/profile_scan.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace skinlab { namespace profile {
namespace {
constexpr uint32_t max_image=128u*1024u*1024u;
constexpr uint32_t max_field=1024u*1024u; // Own conservative admission bound.
using Clock=std::chrono::steady_clock;
bool within(uint64_t start, uint64_t size, uint64_t end) {
    return start<=end && size<=end-start;
}
int hex(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
bool admitted(const Snapshot& s,uint32_t rva,size_t size,bool executable) {
    if(!within(rva,size,s.image_size))return false;
    uint64_t next=rva,end=next+size;
    for(const auto& range:s.ranges) {
        if(range.rva>next)break;
        const uint64_t range_end=uint64_t(range.rva)+range.size;
        if(next<range_end) {
            if(!range.readable || (executable&&!range.executable))return false;
            next=std::min(end,range_end);
            if(next==end)return true;
        }
    }
    return false;
}
uint32_t u32(const uint8_t* p) { uint32_t n;std::memcpy(&n,p,4);return n; }
int32_t i32(const uint8_t* p) { int32_t n;std::memcpy(&n,p,4);return n; }
bool resolve(const Snapshot& s,const Rule& rule,uint32_t at,const uint8_t* bytes,
             Candidate& out,std::string& error) {
    int64_t target=0;
    switch(rule.recipe) {
    case Recipe::RipRelative:
        target=int64_t(at)+7+i32(bytes+3);
        if(target<=0||target>UINT32_MAX||!admitted(s,uint32_t(target),8,false)) {
            error="RIP target is not eight readable in-image bytes";return false;
        }
        out.rva=uint32_t(target);return true;
    case Recipe::CallOrEntry:
        target=bytes[0]==0xe8?int64_t(at)+5+i32(bytes+1):int64_t(at);
        if(target<=0||target>UINT32_MAX||!admitted(s,uint32_t(target),1,true)) {
            error="Function target is outside readable executable image ranges";return false;
        }
        out.rva=uint32_t(target);return true;
    case Recipe::FirstWildcardU32:
        out.field_offset=u32(bytes+rule.first_wildcard);
        break;
    case Recipe::SkinEncoding:
        out.rotate_first=bytes[2];out.xor_key=u32(bytes+9);
        out.rotate_second=bytes[0x17];out.field_offset=u32(bytes+0x21);
        if(out.rotate_first>=32||out.rotate_second>=32) {
            error="Rotate immediate is outside the recovered 0..31 admission";return false;
        }
        break;
    }
    if(out.field_offset==0||out.field_offset>max_field) {
        error="Field offset fails the observer's conservative 1..1MiB admission";return false;
    }
    return true;
}
void validate(const Snapshot& s,const std::vector<Rule>& rules,uint32_t budget) {
    if(!s.image_size||s.image_size>max_image||rules.empty()||rules.size()>64||!budget||budget>10000)
        throw std::invalid_argument("Invalid snapshot size, rule count or time budget");
    uint64_t previous=0,total=0;
    for(const auto& range:s.ranges) {
        if(!range.size||range.rva<previous||!within(range.rva,range.size,s.image_size))
            throw std::invalid_argument("Overlapping or out-of-image admission ranges");
        previous=uint64_t(range.rva)+range.size;
    }
    previous=0;
    for(const auto& region:s.regions) {
        if(region.bytes.empty()||region.rva<previous||!within(region.rva,region.bytes.size(),s.image_size)
           ||!admitted(s,region.rva,region.bytes.size(),true))
            throw std::invalid_argument("Invalid or overlapping executable snapshot regions");
        previous=uint64_t(region.rva)+region.bytes.size();total+=region.bytes.size();
    }
    if(total!=s.copied_bytes||total>max_image||s.expected_bytes>max_image
       ||(s.complete&&(!s.gaps.empty()||s.copied_bytes!=s.expected_bytes||!s.error.empty())))
        throw std::invalid_argument("Inconsistent snapshot coverage");
    for(const auto& r:rules) {
        if(r.name.empty()||r.bytes.empty()||r.bytes.size()>256||r.fixed.size()!=r.bytes.size()
           ||!r.anchor_size||!within(r.anchor_start,r.anchor_size,r.bytes.size()))
            throw std::invalid_argument("Malformed compiled rule");
        for(size_t i=0;i<r.anchor_size;++i)
            if(!r.fixed[r.anchor_start+i])throw std::invalid_argument("Wildcard in anchor");
        size_t required=0;
        switch(r.recipe) {
        case Recipe::RipRelative:required=7;break;
        case Recipe::CallOrEntry:required=5;break;
        case Recipe::FirstWildcardU32:
            if(r.first_wildcard>=r.fixed.size()||r.fixed[r.first_wildcard])
                throw std::invalid_argument("Missing first wildcard");
            required=r.first_wildcard+4;break;
        case Recipe::SkinEncoding:required=0x25;break;
        default:throw std::invalid_argument("Unknown recipe");
        }
        if(r.bytes.size()<required)throw std::invalid_argument("Recipe reads beyond pattern");
    }
}
}
Rule make_rule(const char* name,const char* pattern,Recipe recipe) {
    if(!name||!*name||!pattern)throw std::invalid_argument("Missing rule text");
    Rule out;out.name=name;out.recipe=recipe;
    std::istringstream input(pattern);std::string token;
    size_t run=0,start=0;out.first_wildcard=std::numeric_limits<size_t>::max();
    while(input>>token) {
        const bool wildcard=token=="?"||token=="??";
        if(!wildcard&&(token.size()!=2||hex(token[0])<0||hex(token[1])<0))
            throw std::invalid_argument("Only full hex bytes and ?/?? wildcards are allowed");
        out.bytes.push_back(wildcard?0:uint8_t(hex(token[0])*16+hex(token[1])));
        out.fixed.push_back(wildcard?0:1);
        if(out.bytes.size()>256)throw std::invalid_argument("Pattern exceeds 256 bytes");
        if(wildcard) {
            if(out.first_wildcard==std::numeric_limits<size_t>::max())out.first_wildcard=out.bytes.size()-1;
            run=0;
        } else {
            if(!run)start=out.bytes.size()-1;
            if(++run>out.anchor_size) {out.anchor_start=start;out.anchor_size=run;}
        }
    }
    if(!out.anchor_size)throw std::invalid_argument("Pattern has no fixed bytes");
    return out;
}
const char* state_name(State s) {
    switch(s) {
    case State::Missing:return "missing";case State::Ambiguous:return "ambiguous";
    case State::Incomplete:return "incomplete";case State::Invalid:return "invalid_candidate";
    case State::Changed:return "changed_on_reread";case State::ReadFailed:return "reread_failed";
    case State::SnapshotOnly:return "unique_snapshot_only";
    case State::RereadEqual:return "unique_reread_equal";
    }
    return "invalid_state";
}
std::vector<Result> analyze(const Snapshot& s,const std::vector<Rule>& rules,
                           ReadRva reader,void* context,uint32_t max_scan_ms) {
    validate(s,rules,max_scan_ms);
    const auto deadline=Clock::now()+std::chrono::milliseconds(max_scan_ms);
    std::vector<Result> results;
    bool expired=false;
    for(const auto& rule:rules) {
        Result result;result.name=rule.name;
        const uint8_t* matched_bytes=nullptr;
        for(const auto& region:s.regions) {
            if(expired||result.matches.size()==2)break;
            if(region.bytes.size()<rule.bytes.size())continue;
            // Bound each anchor search to 64 KiB candidate starts; include the
            // complete pattern at the end so page/chunk boundaries are not lost.
            const size_t starts=region.bytes.size()-rule.bytes.size()+1;
            for(size_t block=0;block<starts&&result.matches.size()<2;block+=65536) {
                if(Clock::now()>=deadline) {expired=true;break;}
                const size_t stop=std::min(starts,block+65536);
                size_t cursor=block;
                while(cursor<stop&&result.matches.size()<2) {
                    const auto begin=region.bytes.begin()+cursor+rule.anchor_start;
                    const auto end=region.bytes.begin()+stop+rule.anchor_start+rule.anchor_size-1;
                    const auto found=std::search(begin,end,rule.bytes.begin()+rule.anchor_start,
                                                rule.bytes.begin()+rule.anchor_start+rule.anchor_size);
                    if(found==end)break;
                    const size_t offset=size_t(found-region.bytes.begin())-rule.anchor_start;
                    bool equal=true;
                    for(size_t i=0;i<rule.bytes.size();++i)
                        if(rule.fixed[i]&&rule.bytes[i]!=region.bytes[offset+i]){equal=false;break;}
                    if(equal) {result.matches.push_back(region.rva+uint32_t(offset));matched_bytes=region.bytes.data()+offset;}
                    cursor=offset+1;
                }
            }
        }
        result.match_count_capped=result.matches.size()==2;
        if(result.match_count_capped) {result.state=State::Ambiguous;result.reason="At least two distinct matches";}
        else if(expired||!s.complete) {result.state=State::Incomplete;result.reason=expired?"Scan deadline exceeded":"Executable snapshot coverage is incomplete";}
        else if(result.matches.empty()) {result.state=State::Missing;result.reason="No match in the complete executable snapshot";}
        else {
            Candidate candidate;
            if(!resolve(s,rule,result.matches[0],matched_bytes,candidate,result.reason))result.state=State::Invalid;
            else if(!reader) {result.state=State::SnapshotOnly;result.reason="No current-memory reread provider; candidate withheld";}
            else {
                std::vector<uint8_t> current(rule.bytes.size());
                if(!reader(context,result.matches[0],current.data(),current.size())) {
                    result.state=State::ReadFailed;result.reason="Current-memory reread failed";
                } else if(std::memcmp(current.data(),matched_bytes,current.size())!=0) {
                    result.state=State::Changed;result.reason="Matched bytes or wildcard immediates changed";
                } else {
                    result.state=State::RereadEqual;result.candidate=candidate;result.has_candidate=true;
                    result.reason="Unique code bytes reread equal; engine semantics remain unverified";
                }
            }
        }
        results.push_back(std::move(result));
    }
    return results;
}
} }
