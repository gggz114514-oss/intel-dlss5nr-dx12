#include "port_network_bridge_backend.cpp"
#include <iostream>
static void require(bool x,const char* s){if(!x)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
    require(argc==2,"manifest argument");auto original=checked_plan(argv[1]);auto p=original;
    std::vector<uint32_t> tail;
    for(auto& pass:p.passes)if(pass.name.find("s37_")!=std::string::npos){auto bytes=read_bytes(pass.spv);tail.resize(bytes.size()/4);std::memcpy(tail.data(),bytes.data(),bytes.size());}
    require(!tail.empty(),"fused post tail shader");
    auto post_bytes=read_bytes(p.passes.back().spv);std::vector<uint32_t> scalar_post(post_bytes.size()/4);std::memcpy(scalar_post.data(),post_bytes.data(),post_bytes.size());
    reshape_plan(p,nr_geometry({1280,720},{1280,720},{2560,1440}));
    for(size_t i=0;i<p.buffers.size();++i){require(p.buffers[i].bytes==original.buffers[i].bytes,p.buffers[i].name.c_str());require(p.buffers[i].clearFirst==original.buffers[i].clearFirst,"720 clear policy unchanged");}
    for(size_t i=0;i<p.passes.size();++i){auto& a=p.passes[i];auto& b=original.passes[i];require(a.push==b.push&&a.gx==b.gx&&a.gy==b.gy&&a.gz==b.gz,a.name.c_str());for(size_t k=0;k<a.bindings.size();++k)require(a.bindings[k].bytes==b.bindings[k].bytes,"720 descriptor identity");}
    for(auto size:{NRSize{640,360},NRSize{853,480},NRSize{854,480},NRSize{960,540},NRSize{1114,626},NRSize{1485,835},NRSize{1707,960},NRSize{1920,1080},NRSize{2227,1253},NRSize{2560,1440},NRSize{3840,2160}}){
        p=original;auto g=nr_geometry(size,size,size);reshape_plan(p,g);
        // Replay the actual Full fused-down whole-half store predicates. This
        // is a spatial write-coverage check, independent of allocation sizes.
        NRSize input=g.levels[4],output=g.levels[5];unsigned gx=(input.w+7)/8,gy=(input.h+11)/8;
        std::vector<bool> covered(size_t(output.pixels()),false);
        for(unsigned wy=0;wy<gy;++wy)for(unsigned wx=0;wx<gx;++wx)for(int sy=0;sy<2;++sy){int x=int(wx)*4,y=int(wy)*4-2+2*sy;
            if(x<0||y<0||x+3>=int(output.w)||y+1>=int(output.h))continue;
            for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<4;++dx)covered[size_t(y+dy)*output.w+x+dx]=true;
        }
        unsigned missing=unsigned(std::count(covered.begin(),covered.end(),false));
        const unsigned last_down=kDownsampleBlocks[sizeof(kDownsampleBlocks)/sizeof(kDownsampleBlocks[0])-1];
        auto target=std::find_if(p.buffers.begin(),p.buffers.end(),[&](const Buffer& b){return b.name=="block"+std::to_string(last_down)+"_out";});
        require(target!=p.buffers.end(),"last down output buffer");
        require(!missing||target->clearFirst,"unwritten logical down rows must be initialized before Split/ViT reads them");
        if(size.w==960&&size.h==540)require(missing==64&&target->clearFirst,"540 has exactly two unwritten rows across width32; clear fixes the reproduced defect");
        auto patched=tail;reshape_tail_spirv(patched,size);unsigned changed=0;for(size_t i=0;i<tail.size();++i)changed+=tail[i]!=patched[i];require(changed==3,"only the post x/y bound and raster-stride literals change; IDs/decorations unchanged");
        auto post_patch=scalar_post;reshape_post_local_size(post_patch,size);changed=0;for(size_t i=0;i<scalar_post.size();++i)changed+=scalar_post[i]!=post_patch[i];require(changed==(nr_post_local_size(size)==32?0u:1u),"only scalar post LocalSize changes for large native grids");
        uint64_t bytes=0;for(auto& b:p.buffers)bytes+=b.bytes;
        for(auto& pass:p.passes){require(pass.gx&&pass.gy&&pass.gz&&pass.gx<=65535&&pass.gy<=65535&&pass.gz<=65535,"dispatch exceeds guaranteed Vulkan grid bounds");for(auto& bind:pass.bindings)require(bind.offset+bind.bytes<=p.buffers[bind.resource].bytes,"descriptor bounds");}
        for(auto& h:p.history){require(h.sourceOffset+h.bytes<=p.buffers[h.source].bytes,"history source bounds");require(h.destinationOffset+h.bytes<=p.buffers[h.destination].bytes,"history destination bounds");}
        std::cout<<size.w<<"x"<<size.h<<" P="<<g.padded.w<<"x"<<g.padded.h<<" tokens="<<g.levels[6].pixels()<<" logical_bytes="<<bytes<<" last_down_unwritten_pixels="<<missing<<" clear="<<target->clearFirst<<"\n";
    }
    std::cout<<"GEOMETRY_CPU_PASS 720 exact buffer/PUSH/grid/descriptor identity; Full/Small own topology; eleven new geometries; no GPU\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
