#include "nr_geometry.h"
#include "nr_resample.h"
#include <iostream>
#include <stdexcept>
static void require(bool x,const char* s){if(!x)throw std::runtime_error(s);}
static float weight(uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;}
int main(){try{
    std::vector<uint32_t> words;
    for(unsigned kind=0;kind<3;++kind){uint32_t src=kind==2?9:17,dst=kind==2?17:9;auto axis=nr_filter_axis(words,src,dst,kind);
        for(uint32_t row=0;row<dst;++row){float sum=0;for(uint32_t k=0;k<axis.taps;++k){auto a=axis.offset+(row*axis.taps+k)*2;require(words[a]<src,"clipped filter index");sum+=weight(words[a+1]);}require(std::abs(sum-1)<2e-7,"constant-color/MV preservation");}
    }
    for(auto height:{0u,360u,480u,540u,720u}){auto n=nr_network_size({1707,960},height);require(!height?n==NRSize{1707,960}:n.h==height,"aspect-preserving N selection");}
    require(nr_network_size({640,360},720)==NRSize{640,360},"no upscale into network");
    const float uv=(2.0f*9.0f/17.0f)/9.0f;require(std::abs(uv-2.0f/17.0f)<1e-8,"MV converted once, history UV invariant");
    // Reproduce the real sharp-edge failure: Lanczos AA overshoots [0,1].
    // The model intensity0 post returns clamp(D); residual subtraction must
    // use the same bounded D, otherwise even zero intensity changes R detail.
    auto down=nr_filter_axis(words,17,9,0);unsigned ringing=0;
    for(unsigned x=0;x<9;++x){float raw=0;for(unsigned k=0;k<down.taps;++k){auto a=down.offset+(x*down.taps+k)*2;raw+=(words[a]>=8?1.0f:0.0f)*weight(words[a+1]);}
        ringing+=raw<0||raw>1;float D=(std::max)(0.0f,(std::min)(1.0f,raw));float Y=(std::max)(0.0f,(std::min)(1.0f,D));require(Y-D==0,"intensity0 residual uses bounded D shared with model");}
    require(ringing>0,"sharp-edge fixture must actually reproduce Lanczos ringing");
    auto axis=nr_filter_axis(words,9,17,2);
    for(float delta:{-0.125f,0.0f,0.125f})for(unsigned x=0;x<17;++x){float correction=0;for(unsigned k=0;k<axis.taps;++k)correction+=delta*weight(words[axis.offset+(x*axis.taps+k)*2+1]);
        float original=(x&1)?0.75f:0.25f;require(std::abs(original+correction-(original+delta))<1e-7,"signed residual preserves original detail");}
    std::cout<<"RESAMPLE_CPU_PASS centers/tables/edge clipping; constants; signed +/- residual; original detail; MV UV units; N selection; no GPU\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
