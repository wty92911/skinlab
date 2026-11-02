// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_OBSERVE_HASH_H
#define SKINLAB_OBSERVE_HASH_H
#include <skinlab/profile_scan.h>
#include <bcrypt.h>
#include <array>
#include <stdexcept>
#include <vector>

// CNG hashing runs outside DllMain. Snapshot framing is little endian on the
// supported Windows x64 platform, and includes RVAs rather than ASLR bases.
struct SlObserveHasher {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    std::vector<UCHAR> state;
    SlObserveHasher() {
        try {
            if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
                throw std::runtime_error("Cannot initialize SHA256 provider");
            DWORD bytes=0,count=0;
            if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&bytes),sizeof(bytes),&count,0)<0||!bytes||bytes>1024*1024)
                throw std::runtime_error("Invalid SHA256 state size");
            state.resize(bytes);
            if(BCryptCreateHash(algorithm,&hash,state.data(),bytes,nullptr,0,0)<0)
                throw std::runtime_error("Cannot create SHA256 state");
        }catch(...){close();throw;}
    }
    SlObserveHasher(const SlObserveHasher&)=delete;
    SlObserveHasher& operator=(const SlObserveHasher&)=delete;
    void close(){if(hash){BCryptDestroyHash(hash);hash=nullptr;}if(algorithm){BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;}}
    ~SlObserveHasher(){close();}
    void add(const void* data,size_t size) {
        if(size>UINT32_MAX||BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),ULONG(size),0)<0)
            throw std::runtime_error("SHA256 update failed");
    }
    std::array<uint8_t,32> finish() {
        std::array<uint8_t,32> out{};
        if(BCryptFinishHash(hash,out.data(),ULONG(out.size()),0)<0)throw std::runtime_error("SHA256 finish failed");
        return out;
    }
};
inline std::string sl_observe_hex(const uint8_t* data,size_t size) {
    const char* hex="0123456789abcdef";std::string out;
    for(size_t i=0;i<size;++i){out+=hex[data[i]>>4];out+=hex[data[i]&15];}return out;
}
inline std::array<uint8_t,32> sl_observe_snapshot_hash(const skinlab::profile::Snapshot& image) {
    SlObserveHasher hash;const char domain[]="SkinLab-code-snapshot-v1";
    hash.add(domain,sizeof(domain)-1);hash.add(&image.image_size,sizeof(image.image_size));
    const uint32_t count=uint32_t(image.regions.size());hash.add(&count,sizeof(count));
    for(const auto& region:image.regions) {
        const uint32_t size=uint32_t(region.bytes.size());
        hash.add(&region.rva,sizeof(region.rva));hash.add(&size,sizeof(size));hash.add(region.bytes.data(),region.bytes.size());
    }
    return hash.finish();
}
#endif
