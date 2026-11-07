#pragma once
#include <cstdint>
#include <algorithm>

struct NRSize { uint32_t w=0,h=0; uint64_t pixels() const {return uint64_t(w)*h;} };
inline bool operator==(NRSize a,NRSize b){return a.w==b.w&&a.h==b.h;}
inline bool operator!=(NRSize a,NRSize b){return !(a==b);}
inline uint32_t nr_up(uint32_t n,uint32_t a){return (n+a-1)/a*a;}
// R: game's active rendering rectangle. N: actual network input. P: model
// padding. O: downstream SR target. All origins are in their own resource.
struct NRGeometry {
    NRSize render,network,padded,output;
    NRSize levels[7]; // model canvas followed by pre, four downs, final head
    uint32_t color=0,controls=0,history=0,motion=0,depth=0,noise=0;
    uint64_t pre_bytes=0,post_bytes=0,frame_bytes=0;
};
inline NRSize nr_network_size(NRSize render,uint32_t height){
    if(!height||height>=render.h)return render;
    return {(std::max)(1u,uint32_t((uint64_t(render.w)*height+render.h/2)/render.h)),height};
}
inline NRGeometry nr_geometry(NRSize render,NRSize network,NRSize output){
    NRGeometry g{};g.render=render;g.network=network;g.output=output;
    uint32_t w=(std::max)(320u,nr_up(network.w,64)),h=(std::max)(320u,nr_up(network.h,64));
    if(w%256==0&&h%256==0)w+=64;
    g.padded={w,h};g.levels[0]=g.padded;
    for(unsigned i=1;i<7;++i)g.levels[i]={nr_up((g.levels[i-1].w+1)/2,4),nr_up((g.levels[i-1].h+1)/2,4)};
    uint32_t a=uint32_t(network.pixels());g.controls=a*4;g.history=g.controls+20;
    g.motion=a*8+20;g.depth=a*10+20;g.noise=a*11+20;
    g.pre_bytes=(uint64_t(g.noise)+g.padded.pixels()*3)*4;
    g.post_bytes=uint64_t(g.noise)*4;g.frame_bytes=uint64_t(a)*32;
    return g;
}
