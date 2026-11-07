#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
struct NRFilterAxis {uint32_t offset=0,taps=0;};
// Same texel centers, clipped indices and normalized float32 tables as the
// retained Triton filter_table. No shader trigonometry or per-pixel table work.
inline NRFilterAxis nr_filter_axis(std::vector<uint32_t>& words,uint32_t source,uint32_t destination,unsigned kind){
    struct Tap {int index;double weight;};std::vector<std::vector<Tap>> rows(destination);
    double ratio=double(source)/destination;uint32_t taps=0;
    auto sinc=[](double x){return x==0?1.0:std::sin(3.14159265358979323846*x)/(3.14159265358979323846*x);};
    for(uint32_t out=0;out<destination;++out){
        auto& row=rows[out];double center=(out+0.5)*ratio-0.5;
        if(source==destination)row.push_back({int(out),1});
        else if(kind==1){double lo=out*ratio,hi=(out+1)*ratio;for(int i=int(std::floor(lo));i<int(std::ceil(hi));++i)row.push_back({i,(std::max)(0.0,(std::min)(hi,double(i+1))-(std::max)(lo,double(i)))});}
        else if(kind==0){double scale=(std::max)(1.0,ratio);for(int i=int(std::ceil(center-2*scale));i<=int(std::floor(center+2*scale));++i){double t=(i-center)/scale;row.push_back({i,std::abs(t)<2?sinc(t)*sinc(t/2):0});}}
        else {int base=int(std::floor(center));for(int i=base-1;i<base+3;++i){double t=std::abs(center-i);row.push_back({i,t<=1?(1.5*t-2.5)*t*t+1:t<2?((-.5*t+2.5)*t-4)*t+2:0});}}
        taps=(std::max)(taps,uint32_t(row.size()));
    }
    NRFilterAxis axis{uint32_t(words.size()),taps};
    for(auto& row:rows){double sum=0;for(auto t:row)sum+=t.weight;
        for(uint32_t i=0;i<taps;++i){uint32_t index=i<row.size()?uint32_t((std::max)(0,(std::min)(int(source)-1,row[i].index))):0;
            float weight=i<row.size()?float(row[i].weight/sum):0;uint32_t bits;std::memcpy(&bits,&weight,4);words.push_back(index);words.push_back(bits);}
    }
    return axis;
}
