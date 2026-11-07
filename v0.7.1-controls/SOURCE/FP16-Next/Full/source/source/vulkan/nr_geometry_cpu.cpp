#include "port_network_bridge_backend.cpp"
#include <iostream>
static void require(bool x,const char* s){if(!x)throw std::runtime_error(s);}

static void shared_routes(const Plan& original,const NRGeometry& g){
    auto p=original;reshape_plan(p,g);auto before=p;share_production_inputs(p,g);
    require(before.history.size()==2&&p.history.size()==1,"shared production one history");
    size_t pre=before.history[0].destination,post=before.history[1].destination;
    require(p.buffers[post].production_unused&&p.buffers[post].bytes==g.post_bytes,"shared omitted post allocation");
    require(!p.buffers[pre].production_unused&&p.buffers[pre].bytes==g.pre_bytes,"shared original pre allocation");
    unsigned moved=0;
    for(size_t i=0;i<p.passes.size();++i){const auto& a=before.passes[i];const auto& z=p.passes[i];
        require(a.push==z.push&&a.spv==z.spv&&a.gx==z.gx&&a.gy==z.gy&&a.gz==z.gz,"shared all225 shader/push/grid unchanged");
        for(size_t j=0;j<a.bindings.size();++j){require(a.bindings[j].bytes==z.bindings[j].bytes&&a.bindings[j].offset==z.bindings[j].offset,"shared descriptor ranges unchanged");
            require(z.bindings[j].resource==(a.bindings[j].resource==post?pre:a.bindings[j].resource),"shared only post-to-pre binding changed");moved+=a.bindings[j].resource==post;}
    }
    require(moved==1,"shared final F21 one prefix binding");
    route_production_history(p,g,std::filesystem::path(original.passes.front().spv).parent_path().parent_path().parent_path().append("manifest").string());
    require(p.passes.front().bindings.size()==before.passes.front().bindings.size()&&p.passes.back().bindings.size()==before.passes.back().bindings.size()+2,"history three new production descriptors");
    require(p.passes.front().bindings[6].binding==6&&p.passes.front().bindings[6].resource==pre,"F01 old binding");
    require(p.passes.back().bindings[8].binding==8&&p.passes.back().bindings[8].resource==pre&&p.passes.back().bindings[9].binding==9&&p.passes.back().bindings[9].resource==before.history[0].source,"F21 old and next distinct buffer resources");
    require(std::filesystem::is_regular_file(p.passes.front().spv)&&std::filesystem::is_regular_file(p.passes.back().spv),"production history variants exist");
    const auto& h=p.history.front();
    require(h.destinationOffset>=g.network.pixels()*16&&h.sourceOffset==g.network.pixels()*16,"history banks do not overlap source color/output color");
    require(h.destinationOffset+h.bytes<=uint64_t(g.motion)*4&&h.sourceOffset+h.bytes==g.frame_bytes,"history banks stay in original reserved ranges");
    require(p.buffers[pre].resident&&p.buffers[h.source].resident,"history banks resident across submissions");
    std::cout<<"SHARED_GEOMETRY "<<g.network.w<<"x"<<g.network.h<<" pre="<<pre<<" post="<<post<<" omitted_logical_bytes="<<g.post_bytes<<" history_bytes="<<p.history[0].bytes<<"\n";
}
static void r11_routes(const Plan& original,NRSize size){
    auto diagnostic=original,production=original;auto g=nr_geometry(size,size,size);
    _putenv_s("NR_DIAGNOSTIC_DEBUG","1");reshape_plan(diagnostic,g);
    _putenv_s("NR_DIAGNOSTIC_DEBUG","0");reshape_plan(production,g);
    for(auto& pass:production.passes){const auto file=std::filesystem::path(pass.spv).filename();
        if(file=="3959cd8b245616f898542a2298e5c26eda8799e1ac7d6d38b8afb629a726360e__flags-f18-c27fa8c9e2e1.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"6b0c6aeb8c720c7d80510ee1db22328dc9933ada925fb2c977d138e9479aac13__f18-c27fa8c9e2e1.spv").string();
        if(file=="24e73fd426bb4b4a137deb9f23385922bc2dcc36398655aaadf0a8b3eff50c6b__flags-f18-eb82244a26dc.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"574726f96ab5c77a352954f563c81140fed5f7a6931b1b95eab7b5a926af40f5__f18-eb82244a26dc.spv").string();
        if(file=="985e01c092dfa250808ceac1fd6f413874a7eade5c45ceb007cb289b616cf2a2__flags-f18-c690b9aa292b.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"ada00b9e6194934c05364337c42dc945c93e394d371167d1a236195cbe8205d9__f18-c690b9aa292b.spv").string();
        if(file=="7bc5435a30b36854e3b5a8d0ed025da6c57ae6725856deb5b7ff2fe1a9636938__flags-f19-7f1ad115cb46.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"65a6d5efef8c8dce3f64f0a1e1c6538001e64cad86c38caa78dfde2237cfac8e__f19-7f1ad115cb46.spv").string();
        if(file=="a4fc47d904ff2d39df6896676838c64c2c32211589ab311ac7eb4646a053cc02__flags-f17-8f3be14045d6.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"24f71b5a9202b0a3421d420d6ed3e0ee25d7b7e6a6733ce4f353cd7bb9aacb81__f17-8f3be14045d6.spv").string();
        if(file=="bab705fa40c959e5fba979e4e15cdfff7f22ed00741a3695c95f11344af18486__flags-production-f20-215aca679aaa.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"8c96a02361201acb3fe7f39ac79809a5c2b1389a15690902a88ac17f9d0e9e98__production-f20-215aca679aaa.spv").string();
        if(file=="e3316538946e89c31fd3d9c47ec0771cb42fa57d1a94a2aed30c136c3ed42688__flags-f03-06844a2eaca3.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"818bc5e8ada32ec8b7ac781db663f2eb4bbd30392a7dabe0ed70f53a409237e3__f03-06844a2eaca3.spv").string();
        if(file=="aae2555f3d082340f479d29a19d8bcd5df8befce3fdf38c37f68de217d10c815__flags-f13-bc629c9b8e24.spv")pass.spv=(std::filesystem::path(pass.spv).parent_path()/"c1585d56bd0b0183638172af94cb0f8a22f9d1f6c20d768e9ba60177f767cf71__f13-bc629c9b8e24.spv").string();
    }
    const std::map<std::string,std::string> routes={{"6a1e52e7061a4a3ca10a102466a56f67d8aaf1cac2975c98067724a7b81a75ab__f04-8aa2595a969b.spv","5f1a6a5ce0432bba87d56a76b9984c8d3f937d8f1a1ad5aa4eae733b04d9d360__production-f04-8aa2595a969b.spv"},{"77c63dc928ba73a7cc6c6c29bb9dbcba887223dabcba48c09e89a88877e78e4a__f05-77c63dc928ba.spv","62d4495bf7bf98be8e7c0d25d9c66d09067b0bb553ef09f1216c8807cbb52c44__production-f05-77c63dc928ba.spv"},{"3b81c98222fef7841519805f6c06cbfd0329109af74d6666e96427dfb30d6407__f07-e45c8204c243.spv","3da3568698bb203fd39286bbd9d8ccf1c5c1274e491c3c803567fb6a121e244c__production-f07-e45c8204c243.spv"},{"48d675eddb8686b043e6c0f600e89f5e60782050ffb2434edb796011e81861af__f04-958c9719869c.spv","1686a782b45c2d1fdfd76a432f1666df373daeb06dffd0c0bed1265c06f3bff3__production-f04-958c9719869c.spv"},{"75a54dd630d8d2771fa97b41efa1fcf779f498fdab4d31743494205946dda503__f05-3c26fc5338aa.spv","ac0a245bcb78376a04792906853dcb537b02e96d12e32f77242fa7e12772e1ca__production-f05-3c26fc5338aa.spv"},{"7c839f246426e9a5d1ee81aade2b2c5a62a1a8f026999f6a5748d90e03986987__f06-7c839f246426.spv","d2364a42f8155bebb9435070d609f81b7c719960f74b76cf826685bca05b5d59__production-f06-7c839f246426.spv"},{"6b5260f6a2a66f2099f4816aee4b50e60ebc5b72fe266a5118e9670d160bd5ef__f07-af2304e5c6e5.spv","4049e3e7a39d755a65716c418f28174220c552b2669afec8f8b6a4e411b44968__production-f07-af2304e5c6e5.spv"},{"1d43e42fe973f02376f5c7f48946540ba2571e4645e14eab86927ab8b1ebb796__f04-dc5f1634de7e.spv","2ff362800c1c984439551259b54185b942b8a4f8133c6d5c5e073fbffbbb83e1__production-f04-dc5f1634de7e.spv"},{"a59b807436e9b3dbb3ea891d4efdf58e87272c863627cee42f10a25629c2d719__f05-236aad961967.spv","f0f97fdb02e7fa12f6b184023d1f2ae47388e86bcc2fd92fd931d9c9636ef41c__production-f05-236aad961967.spv"},{"10cfd7d20aaa75949cd590ef5dc5bcc47e80634c4ce651ec957fce923136668c__f06-8f592cf9576f.spv","eccc7bc21a0ed9a50729856d363679dba909b75b773958efe5f109854914b62b__production-f06-8f592cf9576f.spv"},{"ea7f13ce0bd29bbe7b36f85c9977481742f8ea27618698e435c34cfab4df503c__f09-p2-m16-reuse.spv","8d7d03250f7f7f779bcde2971a7c2bd7b7753e59b059150a9ec7dc4f84109e53__production-f09-p2-m16-reuse.spv"},{"0de78f3f8407db1a526f765a422eaed5a1c81196f365fafa33c7b3b17077ac7b__f10-l2-q8.spv","7a1319889095424aff2ac25d6916c529beea016ad5b138d9c5f90584b7a12e9d__production-f10-l2-q8.spv"},{"e8747d59afef4bdd97625e4d627b442517a22197c1f98503a51ab3d5bb144fda__f13-e8747d59afef.spv","5465a87ee1619c0666a9318fdac60669dbd6207018e762a68c13c09774e1a055__production-f13-e8747d59afef.spv"},{"feeedafaedf30b47fe98de1c0e2f5a6dabaf9fa1a5f19f756c3628362e1396d5__f14-72e2df7c7b1e.spv","340096e089167ffc5a6b2566e5590ac450003a1c9e3143688924487cc4b51cf1__production-f14-72e2df7c7b1e.spv"},{"1550b0db0bb720294ad3f474a2181527ea97e78bb417cdcb47dcd70baf31f7c3__f15-q8-axis.spv","66e9599fdec1d0b8ff29c9fe346a90a1f5a11e586589616366e3e94f039c60e7__production-f15-q8-axis.spv"},{"eff2f4ca834269335e3ba8f86dac63d6971e7981088e6c690d68c24625d560b9__f18-572009f4c09f.spv","d9de637be4e6df4390a8d6c25af0ec20228dba1a4d75e9e6d6a16056888ebdc8__production-f18-572009f4c09f.spv"},{"47b20f2fe57d8f3e59dcc2bb982acfec67c9138a5ad4009412217f495c6b2ff0__f18-47b20f2fe57d.spv","bd93203048180682b3fa924246be92d97d913b640a857376ae83d468de1bcba6__production-f18-47b20f2fe57d.spv"},{"bfe7f896332ebd9502c644110dcc539f6f83e591df9c8e0e5c50f907dfd3b6d1__f18-12569d767adf.spv","5043e3cf989783f9f7b6d80c0a9ff8ca5417add5bcbaf3802845c5f7cc15f3e7__production-f18-12569d767adf.spv"},{"7f4caa589198a2f137f54cae9a9d71b36a9864c15957075819595ec55f69af6f__f18-ffde3c3d2379.spv","1d9003468817837f9fd80330fa12f7b63892c88b56ed66f7bad6179d1990bb92__production-f18-ffde3c3d2379.spv"},{"b9d34483ee3c0e31de1dd6f80b1a5faa1a60c7ed23e51218968f29c479d46327__f18-542231a6d6f7.spv","e273ee6440b8724deeaaf67781075ee1c33a75354ed22f4775ccd84e00217c41__production-f18-542231a6d6f7.spv"},{"af74ba2043c4b82a416d937b18b73fee17bdc1988fcfa259a03d8285f1c4babf__f18-af74ba2043c4.spv","251fc3812c6dac7065d71956d7673ab66b15778291f95f1ff72181b95cb67184__production-f18-af74ba2043c4.spv"},{"c204ddbab1a149fcb4334fbb611c56ffccac7d954e311fb8428e29bd688921e6__f20-215aca679aaa.spv","8c96a02361201acb3fe7f39ac79809a5c2b1389a15690902a88ac17f9d0e9e98__production-f20-215aca679aaa.spv"}};unsigned changed=0;
    for(size_t i=0;i<diagnostic.passes.size();++i){auto& d=diagnostic.passes[i];auto& p=production.passes[i];auto filename=std::filesystem::path(d.spv).filename().string();auto it=routes.find(filename);
        require(p.push==d.push&&p.gx==d.gx&&p.gy==d.gy&&p.gz==d.gz,"R11 route retains PUSH/grid and other flags");
        require(p.bindings.size()==d.bindings.size(),"R11 descriptor count");
        auto expected=it==routes.end()?filename:it->second;require(std::filesystem::path(p.spv).filename()==expected,"R11 exact production or diagnostic route");
        require(std::filesystem::is_regular_file(p.spv)&&std::filesystem::is_regular_file(d.spv),"R11 route asset exists");if(it!=routes.end())++changed;
    }
    if(size==NRSize{1280,720})require(changed==153,"R11 all frozen current720 calls routed");
    std::cout<<"R11_ROUTES "<<size.w<<"x"<<size.h<<" changed="<<changed<<" diagnostic retained; flags/PUSH/grid unchanged\n";
}

int main(int argc,char** argv){try{
    require(argc==2,"manifest argument");auto original=checked_plan(argv[1]);auto p=original;r11_routes(original,{1280,720});shared_routes(original,nr_geometry({1280,720},{1280,720},{2560,1440}));
    std::vector<uint32_t> tail;
    for(auto& pass:p.passes)if(pass.name.find("s37_")!=std::string::npos){auto bytes=read_bytes(pass.spv);tail.resize(bytes.size()/4);std::memcpy(tail.data(),bytes.data(),bytes.size());}
    require(!tail.empty(),"fused post tail shader");
    auto post_bytes=read_bytes(p.passes.back().spv);std::vector<uint32_t> scalar_post(post_bytes.size()/4);std::memcpy(scalar_post.data(),post_bytes.data(),post_bytes.size());
    reshape_plan(p,nr_geometry({1280,720},{1280,720},{2560,1440}));
    for(size_t i=0;i<p.buffers.size();++i){require(p.buffers[i].bytes==original.buffers[i].bytes,p.buffers[i].name.c_str());require(p.buffers[i].clearFirst==(std::regex_match(p.buffers[i].name,std::regex("split16_b[0-9]+_T"))?false:original.buffers[i].clearFirst),"720 C14 T clear only; other clear policy unchanged");}
    for(size_t i=0;i<p.passes.size();++i){auto& a=p.passes[i];auto& b=original.passes[i];require(a.push==b.push&&a.gx==b.gx&&a.gy==b.gy&&a.gz==b.gz,a.name.c_str());for(size_t k=0;k<a.bindings.size();++k)require(a.bindings[k].bytes==b.bindings[k].bytes,"720 descriptor identity");}
    for(auto size:{NRSize{640,360},NRSize{853,480},NRSize{854,480},NRSize{960,540},NRSize{1114,626},NRSize{1485,835},NRSize{1707,960},NRSize{1920,1080},NRSize{2227,1253},NRSize{2560,1440},NRSize{3840,2160}}){
        r11_routes(original,size);shared_routes(original,nr_geometry(size,size,size));p=original;auto g=nr_geometry(size,size,size);reshape_plan(p,g);
        for(size_t i=0;i<p.buffers.size();++i)
            if(std::regex_match(p.buffers[i].name,std::regex("split16_b[0-9]+_T")))
                require(p.buffers[i].clearFirst==(original.buffers[i].clearFirst&&g.levels[5].pixels()%32!=0),"C14 retains T clear when projection reads padding");
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
        for(auto& pass:p.passes){if(std::filesystem::path(pass.spv).filename()=="05439e1dfb4a8a5c680b42b130ff7adc51df8bf782fdbbf600ab3705939626f2__f09-l1-n32.spv")require(pass.gy==2,"L1 N32 channel grid");if(std::filesystem::path(pass.spv).filename()=="2039e5c7ab2f77eb7f9702c06d4cd4168c331650a4b2ad899370fd488c540a9f__f16-55e04be51248.spv")require(pass.gy==32,"ViT projection N32 channel grid");if(pass.name.find("_ffn_contract")!=std::string::npos)require(pass.gx==(pass.push[0]+63)/64,"ViT M64 contract grid");if(std::filesystem::path(pass.spv).filename()=="1550b0db0bb720294ad3f474a2181527ea97e78bb417cdcb47dcd70baf31f7c3__f15-q8-axis.spv")require(pass.shared==16384,"ViT Q8 double bank SLM");require(pass.gx&&pass.gy&&pass.gz&&pass.gx<=65535&&pass.gy<=65535&&pass.gz<=65535,"dispatch exceeds guaranteed Vulkan grid bounds");for(auto& bind:pass.bindings)require(bind.offset+bind.bytes<=p.buffers[bind.resource].bytes,"descriptor bounds");}
        for(auto& h:p.history){require(h.sourceOffset+h.bytes<=p.buffers[h.source].bytes,"history source bounds");require(h.destinationOffset+h.bytes<=p.buffers[h.destination].bytes,"history destination bounds");}
        std::cout<<size.w<<"x"<<size.h<<" P="<<g.padded.w<<"x"<<g.padded.h<<" tokens="<<g.levels[6].pixels()<<" logical_bytes="<<bytes<<" last_down_unwritten_pixels="<<missing<<" clear="<<target->clearFirst<<"\n";
    }
    std::cout<<"GEOMETRY_CPU_PASS 720 exact buffer/PUSH/grid/descriptor identity; Full/Small own topology; eleven new geometries; no GPU\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
