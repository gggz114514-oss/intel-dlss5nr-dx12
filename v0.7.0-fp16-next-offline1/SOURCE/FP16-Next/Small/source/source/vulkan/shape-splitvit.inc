// Insert once at END of Small reshape_plan, after generic dimensions/descriptors.
// Input graph uses the ordinary keys in PATCH-MANIFEST.json. No Full block IDs.
// Generic Small PUSH values, bindings, weight scales and all non-owned routes stay intact.
{
    const bool diagnostic=std::getenv("NR_DIAGNOSTIC_DEBUG") && std::string(std::getenv("NR_DIAGNOSTIC_DEBUG"))=="1";
    for(auto& pass:plan.passes) {
        auto& p=pass.push;
        const auto file=std::filesystem::path(pass.spv).filename();
        const auto directory=std::filesystem::path(pass.spv).parent_path();
        if(file=="ea7f13ce0bd29bbe7b36f85c9977481742f8ea27618698e435c34cfab4df503c__small-sv-l0-p2.spv") {
            pass.shared=8192;
            if(!diagnostic)pass.spv=(directory/"8d7d03250f7f7f779bcde2971a7c2bd7b7753e59b059150a9ec7dc4f84109e53__small-sv-production-l0-p2.spv").string();
        }
        if(file=="05439e1dfb4a8a5c680b42b130ff7adc51df8bf782fdbbf600ab3705939626f2__small-sv-l1-n32.spv") {
            pass.gy=2; pass.shared=0;
        }
        if(file=="228dee8a437dd0827c4c7bc770707e645c753d493252fdda0907283889a9eb0e__small-sv-l2-standard.spv") {
            pass.shared=20480;
            if(g.network==NRSize{1280,720}) {pass.spv=(directory/"0de78f3f8407db1a526f765a422eaed5a1c81196f365fafa33c7b3b17077ac7b__small-sv-l2-q8.spv").string(); pass.shared=12288;
                if(!diagnostic)pass.spv=(directory/"7a1319889095424aff2ac25d6916c529beea016ad5b138d9c5f90584b7a12e9d__small-sv-production-l2-q8.spv").string();
            }
        }
        if(file=="cc70e2d68b85c1d3ba2cff1e22e5dbb32562bdc836e4d0651af889da5ddd0015__small-sv-l3.spv") {
        }
        if(file=="d9f2ac3eb45e31d7a3a3600833a33ee5cc5652a5efc3057eb57a6698cdcd9638__small-sv-l3-pool.spv") {
        }
        if(file=="7407f93cd5185d7bbba060e3a5fe705d641de31b934e76251527e1674ade62d5__small-sv-l4-head.spv") {
        }
        if(file=="e8747d59afef4bdd97625e4d627b442517a22197c1f98503a51ab3d5bb144fda__small-sv-vit-expand.spv") {
            if(!diagnostic)pass.spv=(directory/"5465a87ee1619c0666a9318fdac60669dbd6207018e762a68c13c09774e1a055__small-sv-production-vit-expand.spv").string();
        }
        if(file=="c1585d56bd0b0183638172af94cb0f8a22f9d1f6c20d768e9ba60177f767cf71__small-sv-vit-contract-m64.spv") {
            pass.gx=(p[0]+63)/64;
            if(!diagnostic && p[2]==1 && (g.network==NRSize{1280,720}||g.network==NRSize{640,360}||g.network==NRSize{853,480}||g.network==NRSize{854,480}||g.network==NRSize{960,540}||g.network==NRSize{1114,626}||g.network==NRSize{1485,835}||g.network==NRSize{1707,960}||g.network==NRSize{1920,1080}||g.network==NRSize{2227,1253}||g.network==NRSize{2560,1440}||g.network==NRSize{3840,2160}))pass.spv=(directory/"aae2555f3d082340f479d29a19d8bcd5df8befce3fdf38c37f68de217d10c815__small-sv-flags-vit-contract-m64.spv").string();
        }
        if(file=="feeedafaedf30b47fe98de1c0e2f5a6dabaf9fa1a5f19f756c3628362e1396d5__small-sv-vit-qkv.spv") {
            if(!diagnostic)pass.spv=(directory/"340096e089167ffc5a6b2566e5590ac450003a1c9e3143688924487cc4b51cf1__small-sv-production-vit-qkv.spv").string();
        }
        if(file=="d98e907d7f37715d4fa48e4d6b559e92fc7ec49ce31633faa1da51f008395197__small-sv-vit-attention.spv") {
            pass.shared=8192;
            if(g.network==NRSize{1280,720}) {pass.spv=(directory/"1550b0db0bb720294ad3f474a2181527ea97e78bb417cdcb47dcd70baf31f7c3__small-sv-vit-attention-q8.spv").string(); pass.shared=16384;
                if(!diagnostic)pass.spv=(directory/"66e9599fdec1d0b8ff29c9fe346a90a1f5a11e586589616366e3e94f039c60e7__small-sv-production-vit-attention-q8.spv").string();
            }
        }
        if(file=="2039e5c7ab2f77eb7f9702c06d4cd4168c331650a4b2ad899370fd488c540a9f__small-sv-vit-projection-n32.spv") {
            pass.gy=32;
        }
    }
    // C14: proved logical T ownership/read domain; never delete pool initialization.
    if(g.levels[5].pixels()%32==0)
        for(auto& buffer:plan.buffers)
            if(std::regex_match(buffer.name,std::regex("split16_b[0-9]+_T")))buffer.clearFirst=false;
}
