#pragma once
#include <algorithm>
namespace WindowGeometry {
struct Rect { int left,top,right,bottom; };
inline Rect Fit(Rect window,Rect work) {
    const int availableWidth=std::max(1,work.right-work.left);
    const int availableHeight=std::max(1,work.bottom-work.top);
    const int width=std::clamp(window.right-window.left,std::min(320,availableWidth),availableWidth);
    const int height=std::clamp(window.bottom-window.top,std::min(180,availableHeight),availableHeight);
    const int left=std::clamp(window.left,work.left,work.right-width);
    const int top=std::clamp(window.top,work.top,work.bottom-height);
    return {left,top,left+width,top+height};
}
}
