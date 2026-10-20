#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The compiled GSC helper ships inside the DLL (user decision 2026-10-14): at startup
// the plugin writes it to Plutonium's storage\t6\scripts\zm, before any map loads,
// and leaves it there when the game closes. Without the plugin the helper returns at
// once (it checks the GnosisPluginLoaded builtin and calls every plugin builtin
// through invokebuiltin), so a leftover copy is harmless.
namespace helper_deploy {
inline constexpr unsigned char bytes[]={
#include "helper_gsc.inc"
};
enum class Result {written,current,failed};
// %LOCALAPPDATA%\Plutonium\storage\t6\scripts\zm\t6gnosis_helper.gsc, found from the
// DLL's own location in Plutonium\plugins.
inline std::string target_path() {
    HMODULE module=nullptr;char path[MAX_PATH]{};
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&target_path),&module)||!GetModuleFileNameA(module,path,MAX_PATH))return {};
    std::string folder(path);
    for(int up=0;up<2;++up) {const auto slash=folder.find_last_of('\\');if(slash==std::string::npos)return {};folder.resize(slash);}
    return folder+"\\storage\\t6\\scripts\\zm\\t6gnosis_helper.gsc";
}
// Writes the helper when the installed copy differs; replaces it whole through a
// temporary file so a crash cannot leave a truncated script.
inline Result deploy() {
    const auto path=target_path();if(path.empty())return Result::failed;
    FILE* file=nullptr;
    if(!fopen_s(&file,path.c_str(),"rb")&&file) {
        std::vector<unsigned char> existing(sizeof(bytes)+1);
        const auto read=std::fread(existing.data(),1,existing.size(),file);std::fclose(file);
        if(read==sizeof(bytes)&&!std::memcmp(existing.data(),bytes,sizeof(bytes)))return Result::current;
    }
    // Create storage\t6\scripts\zm when missing (each level; existing ones are fine).
    for(auto at=path.find("\\storage\\");at!=std::string::npos;at=path.find('\\',at+1))
        CreateDirectoryA(path.substr(0,at).c_str(),nullptr);
    const auto temporary=path+".tmp";
    if(fopen_s(&file,temporary.c_str(),"wb")||!file)return Result::failed;
    const bool written=std::fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes);
    if(std::fclose(file)!=0||!written||!MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)) {DeleteFileA(temporary.c_str());return Result::failed;}
    return Result::written;
}
}
