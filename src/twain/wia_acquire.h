#pragma once
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

namespace lide25 {
constexpr double bed_width_mm=8.464*25.4;
constexpr double bed_height_mm=11.690*25.4;
struct Settings {
    int dpi=300, pixel=2, brightness=0, contrast=0;
    double left=0, top=0, width=bed_width_mm, height=bed_height_mm;
};
struct Pixels {int x,y,width,height;};
inline Pixels geometry(const Settings&s){
    int max_x=(int)std::floor(8.464*s.dpi+1e-7),max_y=(int)std::floor(11.690*s.dpi+1e-7);
    int x=(int)std::lround(s.left*s.dpi/25.4),y=(int)std::lround(s.top*s.dpi/25.4);
    return {x,y,std::min((int)std::lround(s.width*s.dpi/25.4),max_x-x),std::min((int)std::lround(s.height*s.dpi/25.4),max_y-y)};
}
struct Image {
    std::vector<BYTE> dib;
    size_t pixels_offset=0;
    int width=0,height=0,depth=0,stride=0;
    bool bottom_up=true;
    void clear(){dib.clear();pixels_offset=0;width=height=depth=stride=0;}
};
using Progress=bool(*)(void*,int);
HRESULT acquire(const Settings&,Image&,Progress,void*);
bool available();
}
