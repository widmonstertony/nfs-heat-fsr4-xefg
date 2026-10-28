#pragma once
#include <cmath>
#include <stdexcept>
namespace HeatProjection {
struct Values {float jitterX,jitterY,verticalFov,nearPlane,farPlane;};
inline Values decode(const float* v,unsigned width,unsigned height){
    // CB rows11..14 and15..18 are world-space inverse view-projections.
    // Solve jitter along both camera basis vectors, not world X/Y diagonals.
    double aa=0,ab=0,bb=0,ad=0,bd=0;
    for(unsigned row=0;row<3;row++){
        const double a=v[44+row*4],b=v[45+row*4],d=double(v[63+row*4])-v[47+row*4];
        if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(d))throw std::runtime_error("nonfinite inverse projection");
        aa+=a*a;ab+=a*b;bb+=b*b;ad+=a*d;bd+=b*d;
    }
    const double det=aa*bb-ab*ab;
    if(aa<1e-6||bb<1e-6||det<1e-10)throw std::runtime_error("degenerate camera basis");
    const double dx=(ad*bb-bd*ab)/det,dy=(bd*aa-ad*ab)/det;
    Values r{float(-dx*width*.5),float(dy*height*.5),float(2*std::atan(std::sqrt(bb))),.06f,20000.f};
    if(!std::isfinite(r.jitterX)||!std::isfinite(r.jitterY)||std::abs(r.jitterX)>1||std::abs(r.jitterY)>1)
        throw std::runtime_error("camera-basis jitter outside validated range: "+std::to_string(r.jitterX)+","+std::to_string(r.jitterY));
    if(std::isfinite(v[58])&&std::isfinite(v[59])&&v[58]>0&&v[59]>0){r.nearPlane=1/v[58];r.farPlane=1/v[59];}
    return r;
}
}
