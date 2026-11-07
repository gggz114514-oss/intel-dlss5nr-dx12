// Spatial derivation for the accepted optimized Full and 2026-10-28 Small.
// Weight addresses, channel constants, dispatch implementations, policies and
// half rounding are retained. Model block IDs come from model_profiles.json.
#include <regex>
#include <cstdio>
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
        if(std::filesystem::path(pass.spv).filename()=="1302caadac8865422d7688e3f9a99e322d00e744aad84de8ea93c14746df0915__f08-32291c7ea61b.spv") {
            pass.spv=(std::filesystem::path(pass.spv).parent_path()/"ea7f13ce0bd29bbe7b36f85c9977481742f8ea27618698e435c34cfab4df503c__f09-p2-m16-reuse.spv").string();
            std::fprintf(stderr,"[nrvk] P2_M16_REUSE network=%ux%u pass=%s asset=%s\n",g.network.w,g.network.h,pass.name.c_str(),pass.spv.c_str());
        }
        if(g.network==NRSize{1280,720}){
            const auto propagation_file=std::filesystem::path(pass.spv).filename();
            if(propagation_file=="228dee8a437dd0827c4c7bc770707e645c753d493252fdda0907283889a9eb0e__f10-228dee8a437d.spv"){
                pass.spv=(std::filesystem::path(pass.spv).parent_path()/"0de78f3f8407db1a526f765a422eaed5a1c81196f365fafa33c7b3b17077ac7b__f10-l2-q8.spv").string();
                std::fprintf(stderr,"[nrvk] PROPAGATION_COMBINED route=f10-l2-q8 network=%ux%u pass=%s asset=%s\n",g.network.w,g.network.h,pass.name.c_str(),pass.spv.c_str());
            }
        }
        const auto candidate_file=std::filesystem::path(pass.spv).filename();
        if(g.network==NRSize{1280,720}){
            if(candidate_file=="d98e907d7f37715d4fa48e4d6b559e92fc7ec49ce31633faa1da51f008395197__f15-c9971af497d2.spv"){
                pass.spv=(std::filesystem::path(pass.spv).parent_path()/"1550b0db0bb720294ad3f474a2181527ea97e78bb417cdcb47dcd70baf31f7c3__f15-q8-axis.spv").string();
                std::fprintf(stderr,"[nrvk] ADV_COMBO network=%ux%u pass=%s asset=%s\n",g.network.w,g.network.h,pass.name.c_str(),pass.spv.c_str());
            }
        }
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
            if(layer==1&&std::filesystem::path(pass.spv).filename()=="05439e1dfb4a8a5c680b42b130ff7adc51df8bf782fdbbf600ab3705939626f2__f09-l1-n32.spv") {
                pass.gx=(p[0]*p[1]+15)/16;pass.gy=2;
                std::fprintf(stderr,"[nrvk] L1_N32 network=%ux%u pass=%s asset=%s\n",g.network.w,g.network.h,pass.name.c_str(),pass.spv.c_str());
            }
            else if(layer==2){pass.gx=p[4];pass.gy=p[5];}
            else if(layer==4)pass.gx=(p[28]*p[29]+15)/16;
            else if(layer==3&&pass.gy==8)pass.gx=(p[0]*p[1]+31)/32;
            else pass.gx=(p[0]*p[1]+15)/16;
        }else if(n.find("vit_")!=std::string::npos){
            uint32_t live=uint32_t(g.levels[6].pixels());p[0]=live;p[1]=nr_up(live,64);
            // Debug is disabled in production. Its unused offsets remain bound
            // to the accepted debug ABI; no debug buffers are allocated here.
            pass.gx=(live+(p[2]==0?31:p[2]==3?63:127))/(p[2]==0?32:p[2]==3?64:128);
            if(std::filesystem::path(pass.spv).filename()=="2039e5c7ab2f77eb7f9702c06d4cd4168c331650a4b2ad899370fd488c540a9f__f16-55e04be51248.spv")pass.gy=32;
            if(std::filesystem::path(pass.spv).filename()=="c1585d56bd0b0183638172af94cb0f8a22f9d1f6c20d768e9ba60177f767cf71__f13-bc629c9b8e24.spv")pass.gx=(live+63)/64;
            if(std::filesystem::path(pass.spv).filename()=="1550b0db0bb720294ad3f474a2181527ea97e78bb417cdcb47dcd70baf31f7c3__f15-q8-axis.spv")pass.shared=16384;
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
        // R11 debug-only production variants; diagnostics retain the G03 assets.
        if(!std::getenv("NR_DIAGNOSTIC_DEBUG") || std::string(std::getenv("NR_DIAGNOSTIC_DEBUG"))!="1") {
            const auto r11_file=std::filesystem::path(pass.spv).filename();
            if(r11_file=="6a1e52e7061a4a3ca10a102466a56f67d8aaf1cac2975c98067724a7b81a75ab__f04-8aa2595a969b.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"5f1a6a5ce0432bba87d56a76b9984c8d3f937d8f1a1ad5aa4eae733b04d9d360__production-f04-8aa2595a969b.spv").string();
            if(r11_file=="77c63dc928ba73a7cc6c6c29bb9dbcba887223dabcba48c09e89a88877e78e4a__f05-77c63dc928ba.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"62d4495bf7bf98be8e7c0d25d9c66d09067b0bb553ef09f1216c8807cbb52c44__production-f05-77c63dc928ba.spv").string();
            if(r11_file=="3b81c98222fef7841519805f6c06cbfd0329109af74d6666e96427dfb30d6407__f07-e45c8204c243.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"3da3568698bb203fd39286bbd9d8ccf1c5c1274e491c3c803567fb6a121e244c__production-f07-e45c8204c243.spv").string();
            if(r11_file=="48d675eddb8686b043e6c0f600e89f5e60782050ffb2434edb796011e81861af__f04-958c9719869c.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"1686a782b45c2d1fdfd76a432f1666df373daeb06dffd0c0bed1265c06f3bff3__production-f04-958c9719869c.spv").string();
            if(r11_file=="75a54dd630d8d2771fa97b41efa1fcf779f498fdab4d31743494205946dda503__f05-3c26fc5338aa.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"ac0a245bcb78376a04792906853dcb537b02e96d12e32f77242fa7e12772e1ca__production-f05-3c26fc5338aa.spv").string();
            if(r11_file=="7c839f246426e9a5d1ee81aade2b2c5a62a1a8f026999f6a5748d90e03986987__f06-7c839f246426.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"d2364a42f8155bebb9435070d609f81b7c719960f74b76cf826685bca05b5d59__production-f06-7c839f246426.spv").string();
            if(r11_file=="6b5260f6a2a66f2099f4816aee4b50e60ebc5b72fe266a5118e9670d160bd5ef__f07-af2304e5c6e5.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"4049e3e7a39d755a65716c418f28174220c552b2669afec8f8b6a4e411b44968__production-f07-af2304e5c6e5.spv").string();
            if(r11_file=="1d43e42fe973f02376f5c7f48946540ba2571e4645e14eab86927ab8b1ebb796__f04-dc5f1634de7e.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"2ff362800c1c984439551259b54185b942b8a4f8133c6d5c5e073fbffbbb83e1__production-f04-dc5f1634de7e.spv").string();
            if(r11_file=="a59b807436e9b3dbb3ea891d4efdf58e87272c863627cee42f10a25629c2d719__f05-236aad961967.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"f0f97fdb02e7fa12f6b184023d1f2ae47388e86bcc2fd92fd931d9c9636ef41c__production-f05-236aad961967.spv").string();
            if(r11_file=="10cfd7d20aaa75949cd590ef5dc5bcc47e80634c4ce651ec957fce923136668c__f06-8f592cf9576f.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"eccc7bc21a0ed9a50729856d363679dba909b75b773958efe5f109854914b62b__production-f06-8f592cf9576f.spv").string();
            if(r11_file=="ea7f13ce0bd29bbe7b36f85c9977481742f8ea27618698e435c34cfab4df503c__f09-p2-m16-reuse.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"8d7d03250f7f7f779bcde2971a7c2bd7b7753e59b059150a9ec7dc4f84109e53__production-f09-p2-m16-reuse.spv").string();
            if(r11_file=="0de78f3f8407db1a526f765a422eaed5a1c81196f365fafa33c7b3b17077ac7b__f10-l2-q8.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"7a1319889095424aff2ac25d6916c529beea016ad5b138d9c5f90584b7a12e9d__production-f10-l2-q8.spv").string();
            if(r11_file=="e8747d59afef4bdd97625e4d627b442517a22197c1f98503a51ab3d5bb144fda__f13-e8747d59afef.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"5465a87ee1619c0666a9318fdac60669dbd6207018e762a68c13c09774e1a055__production-f13-e8747d59afef.spv").string();
            if(r11_file=="feeedafaedf30b47fe98de1c0e2f5a6dabaf9fa1a5f19f756c3628362e1396d5__f14-72e2df7c7b1e.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"340096e089167ffc5a6b2566e5590ac450003a1c9e3143688924487cc4b51cf1__production-f14-72e2df7c7b1e.spv").string();
            if(r11_file=="1550b0db0bb720294ad3f474a2181527ea97e78bb417cdcb47dcd70baf31f7c3__f15-q8-axis.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"66e9599fdec1d0b8ff29c9fe346a90a1f5a11e586589616366e3e94f039c60e7__production-f15-q8-axis.spv").string();
            if(r11_file=="eff2f4ca834269335e3ba8f86dac63d6971e7981088e6c690d68c24625d560b9__f18-572009f4c09f.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"d9de637be4e6df4390a8d6c25af0ec20228dba1a4d75e9e6d6a16056888ebdc8__production-f18-572009f4c09f.spv").string();
            if(r11_file=="47b20f2fe57d8f3e59dcc2bb982acfec67c9138a5ad4009412217f495c6b2ff0__f18-47b20f2fe57d.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"bd93203048180682b3fa924246be92d97d913b640a857376ae83d468de1bcba6__production-f18-47b20f2fe57d.spv").string();
            if(r11_file=="bfe7f896332ebd9502c644110dcc539f6f83e591df9c8e0e5c50f907dfd3b6d1__f18-12569d767adf.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"5043e3cf989783f9f7b6d80c0a9ff8ca5417add5bcbaf3802845c5f7cc15f3e7__production-f18-12569d767adf.spv").string();
            if(r11_file=="7f4caa589198a2f137f54cae9a9d71b36a9864c15957075819595ec55f69af6f__f18-ffde3c3d2379.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"1d9003468817837f9fd80330fa12f7b63892c88b56ed66f7bad6179d1990bb92__production-f18-ffde3c3d2379.spv").string();
            if(r11_file=="b9d34483ee3c0e31de1dd6f80b1a5faa1a60c7ed23e51218968f29c479d46327__f18-542231a6d6f7.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"e273ee6440b8724deeaaf67781075ee1c33a75354ed22f4775ccd84e00217c41__production-f18-542231a6d6f7.spv").string();
            if(r11_file=="af74ba2043c4b82a416d937b18b73fee17bdc1988fcfa259a03d8285f1c4babf__f18-af74ba2043c4.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"251fc3812c6dac7065d71956d7673ab66b15778291f95f1ff72181b95cb67184__production-f18-af74ba2043c4.spv").string();
            if(r11_file=="c204ddbab1a149fcb4334fbb611c56ffccac7d954e311fb8428e29bd688921e6__f20-215aca679aaa.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"8c96a02361201acb3fe7f39ac79809a5c2b1389a15690902a88ac17f9d0e9e98__production-f20-215aca679aaa.spv").string();
        }
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
    // C14: L2 writes every logical T token. The swapped projection reads
    // ceil(logical_tokens/32)*32 tokens, so retain initialization when that
    // reader reaches padding. Other buffers keep their accepted clear policy.
    if(g.levels[5].pixels()%32==0)
        for(auto& buffer:plan.buffers)
            if(std::regex_match(buffer.name,std::regex("split16_b[0-9]+_T")))buffer.clearFirst=false;
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

    // FLAGS: exact production-only flag/layout/row domain; ordinary/diagnostic/other shape paths retained.
    if((g.network==NRSize{1280,720}||g.network==NRSize{640,360}||g.network==NRSize{853,480}||g.network==NRSize{854,480}||g.network==NRSize{960,540}||g.network==NRSize{1114,626}||g.network==NRSize{1485,835}||g.network==NRSize{1707,960}||g.network==NRSize{1920,1080}||g.network==NRSize{2227,1253}||g.network==NRSize{2560,1440}||g.network==NRSize{3840,2160}) && (!std::getenv("NR_DIAGNOSTIC_DEBUG") || std::string(std::getenv("NR_DIAGNOSTIC_DEBUG"))!="1"))
    for(auto& pass:plan.passes){auto& p=pass.push;const auto file=std::filesystem::path(pass.spv).filename();
        if(file=="6b0c6aeb8c720c7d80510ee1db22328dc9933ada925fb2c977d138e9479aac13__f18-c27fa8c9e2e1.spv" && p[23]==0)pass.spv=(std::filesystem::path(pass.spv).parent_path()/"3959cd8b245616f898542a2298e5c26eda8799e1ac7d6d38b8afb629a726360e__flags-f18-c27fa8c9e2e1.spv").string();
        if(file=="574726f96ab5c77a352954f563c81140fed5f7a6931b1b95eab7b5a926af40f5__f18-eb82244a26dc.spv" && p[23]==0)pass.spv=(std::filesystem::path(pass.spv).parent_path()/"24e73fd426bb4b4a137deb9f23385922bc2dcc36398655aaadf0a8b3eff50c6b__flags-f18-eb82244a26dc.spv").string();
        if(file=="ada00b9e6194934c05364337c42dc945c93e394d371167d1a236195cbe8205d9__f18-c690b9aa292b.spv" && p[23]==0)pass.spv=(std::filesystem::path(pass.spv).parent_path()/"985e01c092dfa250808ceac1fd6f413874a7eade5c45ceb007cb289b616cf2a2__flags-f18-c690b9aa292b.spv").string();
        if(file=="65a6d5efef8c8dce3f64f0a1e1c6538001e64cad86c38caa78dfde2237cfac8e__f19-7f1ad115cb46.spv" && p[23]==0)pass.spv=(std::filesystem::path(pass.spv).parent_path()/"7bc5435a30b36854e3b5a8d0ed025da6c57ae6725856deb5b7ff2fe1a9636938__flags-f19-7f1ad115cb46.spv").string();
        if(file=="24f71b5a9202b0a3421d420d6ed3e0ee25d7b7e6a6733ce4f353cd7bb9aacb81__f17-8f3be14045d6.spv" && p[28]==0 && uint64_t(pass.gx)*8==uint64_t(p[2])*p[3])pass.spv=(std::filesystem::path(pass.spv).parent_path()/"a4fc47d904ff2d39df6896676838c64c2c32211589ab311ac7eb4646a053cc02__flags-f17-8f3be14045d6.spv").string();
        if(file=="8c96a02361201acb3fe7f39ac79809a5c2b1389a15690902a88ac17f9d0e9e98__production-f20-215aca679aaa.spv" && p[18]==1)pass.spv=(std::filesystem::path(pass.spv).parent_path()/"bab705fa40c959e5fba979e4e15cdfff7f22ed00741a3695c95f11344af18486__flags-production-f20-215aca679aaa.spv").string();
        if(file=="818bc5e8ada32ec8b7ac781db663f2eb4bbd30392a7dabe0ed70f53a409237e3__f03-06844a2eaca3.spv" && p[8]==0 && p[9]==0 && p[2]%4==0 && p[3]%4==0 && uint64_t(pass.gx)*8==uint64_t(p[2])*p[3])pass.spv=(std::filesystem::path(pass.spv).parent_path()/"e3316538946e89c31fd3d9c47ec0771cb42fa57d1a94a2aed30c136c3ed42688__flags-f03-06844a2eaca3.spv").string();
        if(file=="c1585d56bd0b0183638172af94cb0f8a22f9d1f6c20d768e9ba60177f767cf71__f13-bc629c9b8e24.spv" && p[2]==1)pass.spv=(std::filesystem::path(pass.spv).parent_path()/"aae2555f3d082340f479d29a19d8bcd5df8befce3fdf38c37f68de217d10c815__flags-f13-bc629c9b8e24.spv").string();
    }
}
