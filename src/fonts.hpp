#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <imgui.h>
#include <string>
#include <vector>

// Smooth text for the HUD and in-world labels. The look follows the game's points
// counter (user request 2026-10-13): Bahnschrift SemiBold Condensed, a named style of
// the variable Bahnschrift font that ships with Windows 10 and later. ImGui's own
// loader (stb_truetype) can only read a variable font's default style, so GDI, which
// selects named styles, rasterises printable ASCII into custom glyphs of the ImGui
// atlas. Without that style the fallback is Consolas Bold through ImGui, and without
// either the default font. Baked at two sizes so neither is scaled far; the F8 menu
// keeps ImGui's default font, which is pixel-exact at its own size.
namespace fonts {
inline ImFont* large=nullptr;  // HUD readouts and other text above 26 px
inline ImFont* labels=nullptr;  // labels
namespace detail {
struct Pending {int rect=0;int width=0,height=0;std::vector<unsigned char> coverage;};  // coverage 0-64 (GGO_GRAY8)
// A glyph-less base font at pixels, filled with GDI glyphs of face; null when GDI
// does not provide that exact face.
inline ImFont* gdi_font(ImFontAtlas& atlas,const wchar_t* face,float pixels,std::vector<Pending>& pending) {
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return nullptr;
    // Positive height = cell height (ascent + descent), the same sizing ImGui uses
    // for its own fonts, so text keeps its size and placement.
    HFONT font=CreateFontW(static_cast<int>(pixels),0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face);
    if(!font) {DeleteDC(dc);return nullptr;}
    const auto previous=SelectObject(dc,font);
    wchar_t chosen[LF_FACESIZE]{};TEXTMETRICW metrics{};
    ImFont* result=nullptr;
    // GDI substitutes another face when the requested one is missing; refuse that.
    if(GetTextFaceW(dc,LF_FACESIZE,chosen)&&!lstrcmpiW(chosen,face)&&GetTextMetricsW(dc,&metrics)) {
        ImFontConfig config;config.SizePixels=pixels;
        static const ImWchar none[]={0xE000,0xE000,0};  // private use: the base font holds no glyphs
        config.GlyphRanges=none;
        result=atlas.AddFontDefault(&config);
        const MAT2 identity{{0,1},{0,0},{0,0},{0,1}};
        for(wchar_t c=32;c<127;++c) {
            GLYPHMETRICS glyph{};
            const DWORD size=GetGlyphOutlineW(dc,c,GGO_GRAY8_BITMAP,&glyph,0,nullptr,&identity);
            if(size==GDI_ERROR)continue;
            Pending item;
            item.width=size?static_cast<int>(glyph.gmBlackBoxX):1;item.height=size?static_cast<int>(glyph.gmBlackBoxY):1;
            item.coverage.assign(static_cast<std::size_t>(item.width)*item.height,0);
            if(size) {
                std::vector<unsigned char> bitmap(size);
                if(GetGlyphOutlineW(dc,c,GGO_GRAY8_BITMAP,&glyph,size,bitmap.data(),&identity)==GDI_ERROR)continue;
                const int pitch=(item.width+3)&~3;  // GGO rows are DWORD aligned
                for(int y=0;y<item.height;++y)for(int x=0;x<item.width;++x)
                    item.coverage[static_cast<std::size_t>(y)*item.width+x]=bitmap[static_cast<std::size_t>(y)*pitch+x];
            }
            // Offset from the top of the line: GDI gives the origin relative to the baseline.
            item.rect=atlas.AddCustomRectFontGlyph(result,c,item.width,item.height,static_cast<float>(glyph.gmCellIncX),
                ImVec2(static_cast<float>(glyph.gmptGlyphOrigin.x),static_cast<float>(metrics.tmAscent-glyph.gmptGlyphOrigin.y)));
            pending.push_back(std::move(item));
        }
    }
    SelectObject(dc,previous);DeleteObject(font);DeleteDC(dc);
    return result;
}
}
// Call after ImGui::CreateContext and before the first frame; the default font stays
// first so the menu keeps it.
inline void load() {
    large=labels=nullptr;
    auto& atlas=*ImGui::GetIO().Fonts;
    atlas.AddFontDefault();
    std::vector<detail::Pending> pending;
    constexpr const wchar_t* face=L"Bahnschrift SemiBold Condensed";
    large=detail::gdi_font(atlas,face,64.0f,pending);
    labels=large?detail::gdi_font(atlas,face,24.0f,pending):nullptr;
    if(large&&labels) {
        // Build now and write the GDI coverage into the RGBA atlas; the DX11 backend
        // uploads this same buffer when it creates the font texture.
        atlas.Build();
        unsigned char* pixels=nullptr;int width=0,height=0;
        atlas.GetTexDataAsRGBA32(&pixels,&width,&height);
        for(const auto& item:pending) {
            const auto* rect=atlas.GetCustomRectByIndex(item.rect);
            if(!pixels||!rect||rect->X+item.width>width||rect->Y+item.height>height)continue;
            for(int y=0;y<item.height;++y)for(int x=0;x<item.width;++x) {
                auto* texel=pixels+(static_cast<std::size_t>(rect->Y+y)*width+rect->X+x)*4;
                texel[0]=texel[1]=texel[2]=255;
                texel[3]=static_cast<unsigned char>(item.coverage[static_cast<std::size_t>(y)*item.width+x]*255/64);
            }
        }
        return;
    }
    // Fallback: Consolas Bold through ImGui's own loader. Any half-built GDI font is
    // left unused.
    large=labels=nullptr;
    char folder[MAX_PATH]{};
    if(!GetWindowsDirectoryA(folder,MAX_PATH))return;
    const std::string path=std::string(folder)+"\\Fonts\\consolab.ttf";
    if(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES)return;
    ImFontConfig config;config.OversampleH=3;config.OversampleV=2;
    large=atlas.AddFontFromFileTTF(path.c_str(),64.0f,&config);
    labels=atlas.AddFontFromFileTTF(path.c_str(),22.0f,&config);
}
// Context teardown frees the atlas.
inline void forget() {large=labels=nullptr;}
inline ImFont* get(float size) {
    ImFont* chosen=size>26?large:labels;
    if(!chosen)chosen=large?large:labels;
    return chosen?chosen:ImGui::GetFont();
}
}
