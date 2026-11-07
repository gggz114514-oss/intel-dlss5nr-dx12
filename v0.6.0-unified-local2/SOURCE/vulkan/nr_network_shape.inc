// Spatial derivation for the accepted optimized Full and 2026-10-28 Small.
// Weight addresses, channel constants, dispatch implementations, policies and
// half rounding are retained. Model block IDs come from model_profiles.json.
#include <regex>
static int nr_signed(uint32_t x){return int32_t(x);}
static uint64_t nr_features(NRSize s,uint32_t c){return uint64_t(nr_up(s.w,8))*nr_up(s.h,8)*c*2;}
static int nr_block(const std::string& name,const char* pattern){
    std::smatch m;return std::regex_search(name,m,std::regex(pattern))?std::stoi(m[1]):-1;
}
static void reshape_tail_spirv(std::vector<uint32_t>& words,NRSize network){
    if(network==NRSize{1280,720})return;
    unsigned width=0,height=0;
    for(size_t pos=5;pos<words.size();pos+=words[pos]>>16){
        if((words[pos]&65535)==43&&(words[pos]>>16)==4){
            if(words[pos+3]==1280){words[pos+3]=network.w;++width;}
            else if(words[pos+3]==720){words[pos+3]=network.h;++height;}
        }
    }
    if(width!=2||height!=1)throw std::runtime_error("fused post spatial constant identity");
}
static uint32_t nr_post_local_size(NRSize network){return (network.pixels()+31)/32<=65535?32:256;}
static void reshape_post_local_size(std::vector<uint32_t>& words,NRSize network){
    uint32_t local=nr_post_local_size(network);if(local==32)return;
    // Pure scalar per-pixel tail: no shared memory, barriers or subgroup math.
    // Only its execution-mode workgroup size changes; all instructions and
    // numerical decorations stay frozen. 1440p/2160p otherwise exceed grid X.
    unsigned changed=0;
    for(size_t pos=5;pos<words.size();pos+=words[pos]>>16)if((words[pos]&65535)==16&&(words[pos]>>16)==6&&words[pos+2]==17){
        if(words[pos+3]!=32||words[pos+4]!=1||words[pos+5]!=1)throw std::runtime_error("scalar post local-size identity");words[pos+3]=local;++changed;
    }
    if(changed!=1)throw std::runtime_error("scalar post execution mode missing");
}
static void reshape_plan(Plan& plan,const NRGeometry& g){
    const auto old=nr_geometry({1280,720},{1280,720},{1280,720});
    auto size_pair=[&](uint32_t& w,uint32_t& h){
        for(unsigned i=0;i<7;++i)if(w==old.levels[i].w&&h==old.levels[i].h){w=g.levels[i].w;h=g.levels[i].h;return;}
        if(w==1280&&h==720){w=g.network.w;h=g.network.h;return;}
        throw std::runtime_error("unmapped spatial extent "+std::to_string(w)+"x"+std::to_string(h));
    };
    auto tiles_pair=[&](uint32_t& w,uint32_t& h){w*=4;h*=4;size_pair(w,h);w/=4;h/=4;};
    auto window=[](uint32_t tiles,int shift){return (tiles*4-4*shift+7)/8;};
    struct BlockShape {NRSize input,output;uint32_t ci=0,co=0;};
    std::map<int,BlockShape> shapes;
    unsigned level=1,up_index=0;
    for(unsigned block=0;block<=kPostBlock;++block){
        auto& s=shapes[int(block)];
        if(block==0){s={g.levels[0],g.levels[1],32,32};continue;}
        uint32_t c=0;for(const auto& b:kSwinBlocks)if(b.block==block)c=b.channels;
        if(c){s={g.levels[level],g.levels[level],c,c};
            for(auto b:kDownsampleBlocks)if(b==block){s.output=g.levels[++level];s.co=c*2;}
            for(auto b:kUpsampleBlocks)if(b==block){level=4-up_index++;s={g.levels[level+1],g.levels[level],c*2,c};}
        }else if(block==kSplitLast){s={g.levels[5],g.levels[6],512,1024};level=6;
        }else if(block==kDecoderBlock){s={g.levels[6],g.levels[5],1024,512};level=5;
        }else if(block==kPostBlock){s={g.levels[1],g.network,32,16};
        }else {bool vit=false;for(auto b:kVitBlocks)vit|=b==block;s={g.levels[level],g.levels[level],vit?1024u:512u,vit?1024u:512u};}
    }
    // Save allocation identities before changing descriptors. Every source
    // descriptor is whole-buffer except the final-head -> ViT live-token view.
    std::vector<uint64_t> original;for(auto& b:plan.buffers)original.push_back(b.bytes);
    std::map<int,uint64_t> windows,old_windows;
    for(auto& pass:plan.passes){
        auto& p=pass.push;auto n=pass.name;
        if(n.find("swin_body")!=std::string::npos&&p[20]>=8192){int block=nr_block(n,"block([0-9]+)_swin");old_windows[block]=uint64_t(pass.gx)*pass.gy*64*(p[20]/128)*2;}
        if(n.find("f3c")!=std::string::npos){for(auto b:kDownsampleBlocks)if(shapes[b].ci==p[24])old_windows[b]=uint64_t(pass.gx)*pass.gy*64*p[24]*2;}
        if(n.find("f1c32")!=std::string::npos){
            tiles_pair(p[0],p[14]);size_pair(p[16],p[17]);size_pair(p[20],p[21]);
            p[19]=g.noise;p[24]=g.controls;p[25]=g.history;p[26]=g.motion;p[27]=g.depth;
            pass.gx=g.padded.w/8;pass.gy=g.padded.h/8;
        }else if(n.find("s37_")!=std::string::npos){
            tiles_pair(p[0],p[14]);size_pair(p[28],p[29]);
            pass.gx=window(p[0],nr_signed(p[1]));pass.gy=window(p[14],nr_signed(p[15]));
        }else if(n.find("f2c32")!=std::string::npos||n.find("swin_body")!=std::string::npos&&p[0]==168&&p[14]==96){
            tiles_pair(p[0],p[14]);pass.gx=window(p[0],nr_signed(p[1]));pass.gy=window(p[14],nr_signed(p[15]));
            if(n.find("f2c32")!=std::string::npos){p[19]=g.levels[1].w/8;size_pair(p[24],p[25]);size_pair(p[26],p[27]);}
        }else if(n.find("split16_")!=std::string::npos){
            size_pair(p[0],p[1]);p[2]=nr_up(p[0],8)*nr_up(p[1],8);
            p[4]=(p[0]-4*nr_signed(p[6])+7)/8;p[5]=(p[1]-4*nr_signed(p[7])+7)/8;
            size_pair(p[28],p[29]);p[30]=nr_up(p[28],8)*nr_up(p[29],8);
            p[22]=p[2]*512;p[23]=p[22]+p[2]*2048;p[24]=p[23]+p[2]*2048;
            int layer=nr_block(n,"_l([0-9]+)");
            if(layer==2){pass.gx=p[4];pass.gy=p[5];}
            else if(layer==4)pass.gx=(p[28]*p[29]+15)/16;
            else if(layer==3&&pass.gy==8)pass.gx=(p[0]*p[1]+31)/32;
            else pass.gx=(p[0]*p[1]+15)/16;
        }else if(n.find("vit_")!=std::string::npos){
            uint32_t live=uint32_t(g.levels[6].pixels());p[0]=live;p[1]=nr_up(live,64);
            // Debug is disabled in production. Its unused offsets remain bound
            // to the accepted debug ABI; no debug buffers are allocated here.
            pass.gx=(live+(p[2]==0?31:p[2]==3?63:127))/(p[2]==0?32:p[2]==3?64:128);
        }else if(n.find("t3_b39")!=std::string::npos){
            size_pair(p[0],p[1]);size_pair(p[2],p[3]);size_pair(p[11],p[12]);size_pair(p[20],p[21]);pass.gx=(p[0]*p[1]+7)/8;
        }else if(n.find("c3x_")!=std::string::npos){
            tiles_pair(p[0],p[1]);p[4]=window(p[0],nr_signed(p[2]));p[5]=window(p[1],nr_signed(p[3]));
            p[19]=p[0]/2+(p[0]%2);size_pair(p[24],p[25]);size_pair(p[26],p[27]);pass.gx=p[4];pass.gy=p[5];
        }else if(n.find("f3c")!=std::string::npos||n.find("swin_body")!=std::string::npos){
            tiles_pair(p[0],p[1]);p[4]=window(p[0],nr_signed(p[2]));p[5]=window(p[1],nr_signed(p[3]));pass.gx=p[4];pass.gy=p[5];
            if(n.find("f3c")!=std::string::npos){size_pair(p[21],p[22]);p[28]=p[0]*2;p[29]=p[1]*2;}
        }else if(n.find("ioport_block")!=std::string::npos){
            size_pair(p[0],p[1]);size_pair(p[2],p[3]);
            if(p[4]==32&&p[5]==16){size_pair(p[20],p[21]);p[24]=g.controls;p[25]=g.history;p[26]=g.motion;p[27]=g.depth;p[29]=g.controls;uint32_t local=nr_post_local_size(g.network);pass.gx=uint32_t((g.network.pixels()+local-1)/local);}
            else {size_pair(p[14],p[15]);pass.gx=(p[2]*p[3]+7)/8;}
        }else throw std::runtime_error("unmapped optimized pass: "+n);
        // All generic Swin intermediate allocations use window-major C64..256.
        if(n.find("swin_body")!=std::string::npos&&p[20]>=8192){
            int block=nr_block(n,"block([0-9]+)_swin");uint32_t c=p[20]/128;
            windows[block]=uint64_t(p[4])*p[5]*64*c*2;
        }
        if(n.find("f3c")!=std::string::npos){int c=int(p[24]);for(auto b:kDownsampleBlocks)if(shapes[b].ci==uint32_t(c))windows[b]=uint64_t(p[4])*p[5]*64*c*2;}
    }
    auto feature=[&](int b){auto s=shapes.at(b);bool vit=false;for(auto v:kVitBlocks)vit|=v==unsigned(b);return vit?s.output.pixels()*1024*2:nr_features(s.output,s.co);};
    for(size_t i=0;i<plan.buffers.size();++i){auto& b=plan.buffers[i];if(b.path!="-")continue;
        int block=-1;
        if(b.bytes==4||b.name.find("debug")!=std::string::npos)continue;
        if(b.name==kPreImageName)b.bytes=g.pre_bytes;
        else if(b.name==kPostImageName)b.bytes=g.post_bytes;
        else if(b.name=="frame_out")b.bytes=g.frame_bytes;
        else if(b.name=="skip0")b.bytes=nr_features(g.padded,32);
        else if((block=nr_block(b.name,"^block([0-9]+)_out"))>=0)b.bytes=feature(block);
        else if((block=nr_block(b.name,"^skip([0-9]+)"))>=0){auto s=shapes.at(block);b.bytes=nr_features(s.input,s.ci);}
        else if((block=nr_block(b.name,"^swin_([0-9]+)_"))>=0)b.bytes=windows.at(block)+original[i]-old_windows.at(block);
        else if((block=nr_block(b.name,"^vit_b(?:lock)?([0-9]+)_"))>=0){auto suffix=b.name.substr(b.name.find_last_of('_')+1);b.bytes=g.levels[6].pixels()*1024*2*(suffix=="A"?4:suffix=="QKV"?3:1);}
        else if((block=nr_block(b.name,"^split16_b([0-9]+)_"))>=0)b.bytes=b.name.find("_pool")!=std::string::npos?g.levels[6].pixels()*512*2:nr_features(g.levels[5],512);
        else if((block=nr_block(b.name,"^ioport_block([0-9]+)_blend"))>=0)b.bytes=feature(block);
        else if(b.name.find("_ioport_projection")!=std::string::npos)b.bytes=nr_features(g.levels[6],512);
        else if(b.name.find("_ioport_blended")!=std::string::npos)b.bytes=nr_features(g.padded,32);
        else if(b.name.find("_ioport_body")!=std::string::npos)b.bytes=g.network.pixels()*16;
        else if((block=nr_block(b.name,"^c3_x_c([0-9]+)"))>=0){
            unsigned l=block==256?4:block==128?3:2;
            if(original[i]==nr_features(old.levels[l],block))b.bytes=nr_features(g.levels[l],block);
            else {for(auto up:kUpsampleBlocks)if(shapes[up].co==unsigned(block))b.bytes=windows.at(up);}
        }else throw std::runtime_error("unmapped spatial buffer: "+b.name);
    }
    // The Full C256 fused down kernel writes complete two-row halves. When
    // round4(input_H/2) adds two rows, no WG owns those final rows. They are
    // live to the following Split/ViT path, so the existing alias buffer must
    // be cleared before its first writer. Small already has this policy.
    const unsigned last_down=kDownsampleBlocks[sizeof(kDownsampleBlocks)/sizeof(kDownsampleBlocks[0])-1];
    if(g.levels[5].h>g.levels[4].h/2){
        const std::string name="block"+std::to_string(last_down)+"_out";
        for(auto& buffer:plan.buffers)if(buffer.name==name)buffer.clearFirst=true;
    }
    for(auto& pass:plan.passes)for(auto& bind:pass.bindings){
        if(bind.offset||bind.bytes!=original.at(bind.resource)){
            if(bind.offset||bind.bytes!=old.levels[6].pixels()*1024*2)throw std::runtime_error("unmapped descriptor slice");
            bind.bytes=g.levels[6].pixels()*1024*2;
        }else bind.bytes=plan.buffers.at(bind.resource).bytes;
    }
    for(auto& h:plan.history){h.sourceOffset=g.network.pixels()*16;h.destinationOffset=uint64_t(g.history)*4;h.bytes=g.network.pixels()*16;h.controlsOffset=uint64_t(g.controls)*4;}
}
