#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>
#include <cstring>

// Reads game memory from inside the game process: a plain copy guarded against access
// violations, so an invalid address returns false instead of crashing. It replaces
// ReadProcessMemory on the plugin's own process (2026-10-20), which antivirus
// heuristics weigh as a malware trait. Only access violations are caught; anything
// else (a guard page, for one) is left to the game.
namespace memory {
inline bool copy(const void* from,void* to,std::size_t size) noexcept {
    __try {std::memcpy(to,from,size);return true;}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return false;}
}
inline bool read(std::uint32_t address,void* out,std::size_t size) noexcept {
    return address>=0x10000&&size<=0xFFFFFFFFu-address&&copy(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),out,size);
}
}
