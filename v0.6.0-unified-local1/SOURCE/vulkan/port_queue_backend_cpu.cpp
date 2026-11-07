// Exercise the actual frame implementation through CPU command spies.
// No VkInstance/Device, Vulkan loader, queue submission, or GPU is created.
#include "port_network_bridge_backend.cpp"
#include <iostream>
#define REQUIRE(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
static uint32_t dispatches=0,fills=0,copies=0,updates=0,dependencies=0,network_index=0,expected_total_dispatches=0;
static VkResult fence_status=VK_SUCCESS;
static uint32_t expected_seed=0;
static bool expected_depth=false;
static bool expected_history=false;
static NRVK_Backend* context;
static void VKAPI_CALL spy_bind(VkCommandBuffer,VkPipelineBindPoint,VkPipeline) {}
static void VKAPI_CALL spy_sets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkDescriptorSet*,uint32_t,const uint32_t*) {}
static void VKAPI_CALL spy_push(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,uint32_t,uint32_t size,const void* data) {
    const auto& g=context->geometry;bool low=g.render!=g.network;uint32_t pre_count=low?2:1;
    if(dispatches<pre_count){
        REQUIRE(size==(low?112:64));
        const auto* p=static_cast<const uint32_t*>(data);
        REQUIRE(p[6]==g.network.w&&p[7]==g.network.h);
        REQUIRE(p[8]==0&&p[9]==g.motion&&p[10]==g.depth);
        REQUIRE(p[11]==uint32_t(expected_depth));
        REQUIRE(p[12]==0&&p[13]==g.motion&&p[14]==g.depth&&p[15]==dispatches);
        if(low)REQUIRE(p[16]==g.render.w&&p[17]==g.render.h);
        return;
    }
    if(dispatches>=pre_count+context->plan.passes.size()){REQUIRE(size==(low?64:16));return;}
    auto push=context->plan.passes[network_index].push;
    if(network_index==0){push[22]=expected_seed;push[23]&=~4u;}
    REQUIRE(size==push.size()*4&&memcmp(push.data(),data,size)==0);
}
static void VKAPI_CALL spy_dispatch(VkCommandBuffer,uint32_t x,uint32_t y,uint32_t z) {
    const auto& g=context->geometry;bool low=g.render!=g.network;uint32_t pre_count=low?2:1;
    if(dispatches<pre_count){uint64_t area=low&&dispatches==0?uint64_t(g.render.w)*g.network.h:g.network.pixels();REQUIRE(x==(area+255)/256&&y==1&&z==1);}
    else if(dispatches>=pre_count+context->plan.passes.size()){uint64_t area=low&&dispatches==expected_total_dispatches-2?uint64_t(g.render.w)*g.network.h:g.render.pixels();REQUIRE(x==(area+255)/256&&y==1&&z==1);}
    else{auto& p=context->plan.passes[network_index++];REQUIRE(x==p.gx&&y==p.gy&&z==p.gz);}
    ++dispatches;
}
static void VKAPI_CALL spy_fill(VkCommandBuffer,VkBuffer,VkDeviceSize offset,VkDeviceSize size,uint32_t value) {REQUIRE(offset==0&&size==VK_WHOLE_SIZE&&value==0);++fills;}
static void VKAPI_CALL spy_copy(VkCommandBuffer,VkBuffer,VkBuffer,uint32_t n,const VkBufferCopy* r) {auto& g=context->geometry;REQUIRE(n==1&&r->size==g.network.pixels()*16&&r->srcOffset==g.network.pixels()*16&&r->dstOffset==uint64_t(g.history)*4);++copies;}
static void VKAPI_CALL spy_update(VkCommandBuffer,VkBuffer,VkDeviceSize offset,VkDeviceSize size,const void* data) {
    auto& g=context->geometry;REQUIRE(offset==uint64_t(g.controls)*4&&size==80);auto controls=static_cast<const float*>(data);
    REQUIRE(controls[8]==float(expected_history));++updates;
    REQUIRE(controls[9]==g.network.w&&controls[10]==g.network.h);
    REQUIRE(controls[11]==float(expected_depth)&&controls[12]==0&&controls[13]==1.0f/float(g.network.w)&&controls[14]==1.0f/float(g.network.h));
}
static void VKAPI_CALL spy_barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t n,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*) {REQUIRE(n==1);++dependencies;}
static void VKAPI_CALL spy_descriptors(VkDevice,uint32_t n,const VkWriteDescriptorSet* w,uint32_t,const VkCopyDescriptorSet*) {
    auto& g=context->geometry;
    if(n==13){REQUIRE(g.render!=g.network&&w[5].pBufferInfo->range==uint64_t(g.render.w)*g.network.h*24&&w[11].pBufferInfo->buffer==w[5].pBufferInfo->buffer&&w[6].pBufferInfo->buffer==w[12].pBufferInfo->buffer);return;}
    REQUIRE(n==7&&w[0].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&w[6].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    REQUIRE(w[3].pBufferInfo->range==g.pre_bytes&&w[4].pBufferInfo->range==g.post_bytes&&w[5].pBufferInfo->range==g.frame_bytes);
}
static VkResult VKAPI_CALL spy_fence(VkDevice,VkFence) {return fence_status;}
int main(int argc,char** argv) {
    try {
        REQUIRE(argc==3);REQUIRE(nrvk_backend_probe(argv[1])==0);
        HMODULE own_dll=LoadLibraryA(argv[2]);REQUIRE(own_dll);
        for(const char* name:{"nrvk_backend_probe","nrvk_backend_init","nrvk_backend_frame","nrvk_backend_release"})REQUIRE(GetProcAddress(own_dll,name));
        auto dll_probe=reinterpret_cast<decltype(&nrvk_backend_probe)>(GetProcAddress(own_dll,"nrvk_backend_probe"));REQUIRE(dll_probe(argv[1])==0);FreeLibrary(own_dll);
        NRVK_Config cfg{};NRVK_Backend* empty=nullptr;
        REQUIRE(nrvk_backend_init(VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,0,&cfg,&empty)==-1);
        REQUIRE(nrvk_backend_frame(nullptr,VK_NULL_HANDLE,nullptr)==-1);nrvk_backend_release(nullptr);
        NRVK_Backend b;context=&b;b.plan=read_plan((std::filesystem::path(argv[1])/"port_network_runtime.txt").string());b.pipelines.resize(1);
        bool found_pre=false,found_post=false;
        for(size_t i=0;i<b.plan.buffers.size();++i) {
            if(b.plan.buffers[i].name=="ioport_0_ioport_images"){b.pre=i;found_pre=true;}
            if(b.plan.buffers[i].name==kPostImageName){b.post=i;found_post=true;}
        }
        REQUIRE(found_pre&&found_post);
        b.output=b.plan.outputs.front().resource;
        REQUIRE(b.plan.buffers.size()==kPlanBuffers);
        REQUIRE(b.plan.passes.size()==kPlanPasses);
        REQUIRE(b.plan.buffers[b.pre].bytes==52936784);
        REQUIRE(b.plan.buffers[b.post].bytes==40550480);
        REQUIRE(b.plan.buffers[b.output].bytes==29491200);
        expected_total_dispatches=uint32_t(b.plan.passes.size())+2;
        b.vkCmdBindPipeline=spy_bind;b.vkCmdBindDescriptorSets=spy_sets;b.vkCmdPushConstants=spy_push;b.vkCmdDispatch=spy_dispatch;
        b.vkCmdFillBuffer=spy_fill;b.vkCmdCopyBuffer=spy_copy;b.vkCmdUpdateBuffer=spy_update;b.vkCmdPipelineBarrier=spy_barrier;
        b.vkUpdateDescriptorSets=spy_descriptors;b.vkGetFenceStatus=spy_fence;
        NRVK_FrameParams params{};params.abi_size=sizeof(params);params.render_width=1280;params.render_height=720;params.output_width=2560;params.output_height=1440;params.low_resolution_motion=1;params.pre_exposure=params.exposure_scale=1;
        params.motion_scale_x=params.motion_scale_y=1;params.motion_scale_origin=NRB_MV_DLSS_NGX;
        VkImageView views[4]={(VkImageView)1,(VkImageView)2,(VkImageView)3,VK_NULL_HANDLE};
        PortNetworkBridgeFrameIO io{};io.v3={sizeof(io),4,views,(VkImageView)4,&params};io.nrb_controls.abi_size=sizeof(NRB_Controls);io.nrb_controls.input_height=720;io.model_controls[8]=1;io.model_controls[9]=1280;io.model_controls[10]=720;
        for(unsigned i=0;i<5;++i){io.images[i].image=(VkImage)(uintptr_t)(i+1);io.images[i].extent={1280,720};io.images[i].valid_rect={{0,0},{1280,720}};io.images[i].layout=VK_IMAGE_LAYOUT_GENERAL;io.images[i].usage=i==4?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT;}
        io.images[0].format=VK_FORMAT_R32G32B32A32_SFLOAT;io.images[1].format=VK_FORMAT_R32G32_SFLOAT;io.images[2].format=VK_FORMAT_R32_SFLOAT;io.images[4].format=VK_FORMAT_R16G16B16A16_SFLOAT;
        views[2]=VK_NULL_HANDLE;io.images[2].image=VK_NULL_HANDLE;io.images[4].format=VK_FORMAT_R32G32B32A32_SFLOAT;
        VkCommandBuffer cb=(VkCommandBuffer)1;
        NRVK_BackendFrameIO v3=io.v3;v3.abi_size=sizeof(v3);REQUIRE(nrvk_backend_frame(&b,cb,&v3)==-6&&dispatches==0);
        const uint32_t clear_count=uint32_t(std::count_if(b.plan.buffers.begin(),b.plan.buffers.end(),[](const Buffer& x){return !x.resident&&x.clearFirst;}));
        REQUIRE(nrvk_backend_frame(&b,cb,&io.v3)==0);REQUIRE(dispatches==expected_total_dispatches&&network_index==b.plan.passes.size()&&fills==clear_count&&copies==0&&updates==2);
        uint32_t deps0=dependencies;REQUIRE(nrvk_backend_frame(&b,cb,&io.v3)==-7&&dispatches==expected_total_dispatches);
        REQUIRE(port_network_bridge_backend_submit_result(&b,(VkFence)1,VK_SUCCESS)==0);
        io.frame_id=1;io.noise_seed=1;expected_seed=1;fence_status=VK_NOT_READY;REQUIRE(nrvk_backend_frame(&b,cb,&io.v3)==-7&&dispatches==expected_total_dispatches);
        fence_status=VK_SUCCESS;dispatches=fills=copies=updates=dependencies=network_index=0;
        views[2]=(VkImageView)3;io.images[2].image=(VkImage)3;expected_depth=true;
        expected_history=true;
        REQUIRE(nrvk_backend_frame(&b,cb,&io.v3)==0);REQUIRE(dispatches==expected_total_dispatches&&network_index==b.plan.passes.size()&&fills==clear_count&&copies==2&&updates==2&&dependencies==deps0);
        REQUIRE(port_network_bridge_backend_submit_result(&b,VK_NULL_HANDLE,VK_ERROR_DEVICE_LOST)==0);REQUIRE(b.committed_frame==0);
        const auto original=b.plan;
        for(const auto& test:std::vector<std::pair<NRSize,uint32_t>>{{{1280,720},360},{{1920,1080},480},{{1707,960},540},{{2560,1440},720},{{1114,626},0},{{3840,2160},0}}){
            NRSize render=test.first,network=nr_network_size(render,test.second);b.geometry=nr_geometry(render,network,render);b.plan=original;reshape_plan(b.plan,b.geometry);
            b.resample.handle=(VkBuffer)1;b.resample.bytes=uint64_t(render.w)*network.h*24;b.history_committed=false;b.last_network_fence=VK_NULL_HANDLE;
            params.render_width=render.w;params.render_height=render.h;params.output_width=render.w;params.output_height=render.h;params.reset_history=0;
            io.nrb_controls.input_height=test.second;io.model_controls[9]=float(network.w);io.model_controls[10]=float(network.h);io.frame_id=100;io.control_epoch=9;
            for(auto& im:io.images){im.extent={render.w,render.h};im.valid_rect={{0,0},{render.w,render.h}};}
            expected_total_dispatches=uint32_t(b.plan.passes.size())+(network==render?2:4);
            dispatches=fills=copies=updates=dependencies=network_index=0;expected_history=false;
            REQUIRE(nrvk_backend_frame(&b,cb,&io.v3)==0&&dispatches==expected_total_dispatches&&network_index==b.plan.passes.size()&&copies==0);
            REQUIRE(port_network_bridge_backend_submit_result(&b,(VkFence)1,VK_SUCCESS)==0);
            ++io.frame_id;dispatches=fills=copies=updates=dependencies=network_index=0;expected_history=true;
            REQUIRE(nrvk_backend_frame(&b,cb,&io.v3)==0&&copies==2);REQUIRE(port_network_bridge_backend_submit_result(&b,(VkFence)1,VK_SUCCESS)==0);
            ++io.frame_id;params.reset_history=1;dispatches=fills=copies=updates=dependencies=network_index=0;expected_history=false;
            REQUIRE(nrvk_backend_frame(&b,cb,&io.v3)==0&&copies==0);REQUIRE(port_network_bridge_backend_submit_result(&b,(VkFence)1,VK_SUCCESS)==0);
        }
        std::cout<<"CPU_ENTRY_PASS probe/hash/basicVMM frozen;"<<expected_total_dispatches<<" dispatches=2 bridge+"<<b.plan.passes.size()<<" network, "<<clear_count<<" fills, "<<deps0<<" memory dependencies/frame; SR2560x1440 with native720 MVLowRes; optional depth controls11; RGBA32F NR; previous-fence gating; submit failure preserves committed history; Vulkan loader/GPU not used\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<" last_error="<<port_network_bridge_backend_last_error()<<"\n";return 1;}
}
