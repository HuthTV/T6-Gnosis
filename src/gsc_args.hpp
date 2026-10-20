#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "memory.hpp"
#include "navigation.hpp"
#include "spawn_fingerprints.hpp"

// Reads the arguments of a Gnosis builtin from the T6 script VM stack. The SDK's
// register_function only supplies a no-argument callback, so this is
// reverse-engineered access: the addresses below come from the engine's own
// GetNumParam/GetInt/GetFloat/GetVector, which spawn_fingerprints checks byte for byte.
// Same contract as the readers in despawn.cpp and spawns.cpp. Call bindings_match()
// once at startup and register nothing if it fails.
namespace gsc_args {
inline bool read(std::uint32_t address,void* out,std::size_t size) {
    if(address<0x10000||size>4096||size>0xFFFFFFFFu-address)return false;
    return memory::read(address,out,size);
}
template<class T> bool read(std::uint32_t address,T& out) {return read(address,&out,sizeof(out));}
inline bool bindings_match() {
    for(const auto& f:spawns::spawn_fingerprints) {
        std::array<unsigned char,128> bytes{};
        if(f.size>bytes.size()||!read(f.address,bytes.data(),f.size)||std::memcmp(bytes.data(),f.bytes,f.size))return false;
    }
    return true;
}
struct Value {std::uint32_t type=0,payload=0;};
static_assert(sizeof(Value)==8);
static_assert(sizeof(nav::Vec3)==12);
// VM value types used here: 4 vector (payload points at three floats), 6 float, 7 int.
struct Arguments {
    std::array<Value,12> slots{};
    std::uint32_t count=0,top=0;
    bool unchanged() const {
        std::uint32_t n=0,p=0;return read(0x2DEA8EC,n)&&read(0x2DEA8E0,p)&&n==count&&p==top;
    }
    bool capture(std::uint32_t expected) {
        constexpr std::uint32_t first=0x2DEAB70,last=0x2DEEB68;
        if(expected>slots.size()||!read(0x2DEA8EC,count)||count!=expected||!read(0x2DEA8E0,top)||
            top<first||top>last||((top-first)&7u)||(expected&&top-first<(expected-1)*8))return false;
        for(unsigned i=0;i<expected;++i)if(!read(top-i*8,slots[i]))return false;
        return unchanged();
    }
    bool integer(unsigned index,int& out) const {
        if(index>=count||slots[index].type!=7)return false;
        std::memcpy(&out,&slots[index].payload,4);return true;
    }
    // Float, or an int promoted to float (GSC literals such as 0 arrive as ints).
    bool number(unsigned index,float& out) const {
        if(index>=count)return false;
        if(slots[index].type==6)std::memcpy(&out,&slots[index].payload,4);
        else {int value=0;if(!integer(index,value))return false;out=static_cast<float>(value);}
        return std::isfinite(out);
    }
    // Finite world-space vector within the map coordinate bound used by the spawn reader.
    bool vector(unsigned index,nav::Vec3& out) const {
        if(index>=count||slots[index].type!=4||!read(slots[index].payload,out)||!nav::finite(out))return false;
        return std::abs(out.x)<=131072&&std::abs(out.y)<=131072&&std::abs(out.z)<=131072&&unchanged();
    }
};
}
