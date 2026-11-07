// Actual production nrvk_backend_frame recording through CPU command spies.
// No Vulkan loader/device, GPU queue, submission, or GPU wait is used.
#include "host_reference_backend.cpp"
#include <iostream>
static bool tracing=false;
static std::ofstream trace_file;
static volatile uint64_t observed=0;
static void event(std::initializer_list<uint64_t> words) {
    observed+=words.size();
    if(tracing){for(auto v:words)trace_file<<v<<' ';trace_file<<'\n';}
}
static void payload(const void* data,uint32_t bytes) {
    if(tracing){const auto* p=static_cast<const uint32_t*>(data);for(uint32_t i=0;i<bytes/4;++i)trace_file<<p[i]<<' ';trace_file<<'\n';}
}
template<class T> static uint64_t h(T x){return uint64_t(uintptr_t(x));}
static void VKAPI_CALL bind(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipeline pipe){event({1,h(cb),uint64_t(point),h(pipe)});}
static void VKAPI_CALL sets(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet* data,uint32_t offsets,const uint32_t* dynamic){event({2,h(cb),uint64_t(point),h(layout),first,count,offsets});if(tracing){for(uint32_t i=0;i<count;++i)event({h(data[i])});for(uint32_t i=0;i<offsets;++i)event({dynamic[i]});}}
static void VKAPI_CALL push(VkCommandBuffer cb,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t bytes,const void* data){event({3,h(cb),h(layout),stages,offset,bytes});payload(data,bytes);}
static void VKAPI_CALL dispatch(VkCommandBuffer cb,uint32_t x,uint32_t y,uint32_t z){event({4,h(cb),x,y,z});}
static void VKAPI_CALL fill(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize bytes,uint32_t value){event({5,h(cb),h(buffer),offset,bytes,value});}
static void VKAPI_CALL copy(VkCommandBuffer cb,VkBuffer source,VkBuffer dest,uint32_t count,const VkBufferCopy* regions){event({6,h(cb),h(source),h(dest),count});if(tracing)for(uint32_t i=0;i<count;++i)event({regions[i].srcOffset,regions[i].dstOffset,regions[i].size});}
static void VKAPI_CALL update(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize bytes,const void* data){event({7,h(cb),h(buffer),offset,bytes});payload(data,uint32_t(bytes));}
static void VKAPI_CALL dependency(VkCommandBuffer cb,VkPipelineStageFlags source,VkPipelineStageFlags dest,VkDependencyFlags flags,uint32_t count,const VkMemoryBarrier* memory,uint32_t buffers,const VkBufferMemoryBarrier*,uint32_t images,const VkImageMemoryBarrier*){event({8,h(cb),source,dest,flags,count,buffers,images});if(tracing)for(uint32_t i=0;i<count;++i)event({uint64_t(memory[i].sType),memory[i].srcAccessMask,memory[i].dstAccessMask});}
static void VKAPI_CALL descriptors(VkDevice device,uint32_t count,const VkWriteDescriptorSet* writes,uint32_t copies,const VkCopyDescriptorSet*){
    event({9,h(device),count,copies});if(!tracing)return;
    for(uint32_t i=0;i<count;++i){const auto& w=writes[i];event({uint64_t(w.sType),h(w.dstSet),w.dstBinding,w.dstArrayElement,w.descriptorCount,uint64_t(w.descriptorType)});
        for(uint32_t j=0;j<w.descriptorCount;++j)if(w.pBufferInfo){const auto& b=w.pBufferInfo[j];event({h(b.buffer),b.offset,b.range});}else if(w.pImageInfo){const auto& im=w.pImageInfo[j];event({h(im.sampler),h(im.imageView),uint64_t(im.imageLayout)});}
    }
}
static VkResult VKAPI_CALL fence(VkDevice device,VkFence f){event({10,h(device),h(f)});return VK_SUCCESS;}
static void VKAPI_CALL timestamp(VkCommandBuffer cb,VkPipelineStageFlagBits stage,VkQueryPool pool,uint32_t index){event({11,h(cb),uint64_t(stage),h(pool),index});}
static void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    require(argc>=4,"manifest trace|bench output [warm measure]");
    Plan original=read_plan((std::filesystem::path(argv[1])/"port_network_runtime.txt").string());
    require(original.passes.size()==225&&original.buffers.size()==495,"Full225/495 identity");
    NRVK_Backend b;
    b.vkCmdBindPipeline=bind;b.vkCmdBindDescriptorSets=sets;b.vkCmdPushConstants=push;b.vkCmdDispatch=dispatch;b.vkCmdFillBuffer=fill;b.vkCmdCopyBuffer=copy;b.vkCmdUpdateBuffer=update;b.vkCmdPipelineBarrier=dependency;b.vkUpdateDescriptorSets=descriptors;b.vkGetFenceStatus=fence;b.vkCmdWriteTimestamp=timestamp;
    b.ts_query_pool=(VkQueryPool)71;b.ts_base_query=10;
    NRVK_FrameParams params{};params.abi_size=sizeof(params);params.pre_exposure=params.exposure_scale=1;params.motion_scale_x=params.motion_scale_y=1;params.motion_scale_origin=NRB_MV_DLSS_NGX;
    VkImageView views[4]={(VkImageView)1,(VkImageView)2,(VkImageView)3,VK_NULL_HANDLE};
    PortNetworkBridgeFrameIO io{};io.v3={sizeof(io),4,views,(VkImageView)4,&params};io.nrb_controls.abi_size=sizeof(NRB_Controls);io.model_controls[8]=1;
    for(unsigned i=0;i<5;++i){io.images[i].image=(VkImage)(uintptr_t)(i+1);io.images[i].layout=VK_IMAGE_LAYOUT_GENERAL;io.images[i].usage=i==4?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT;}
    io.images[0].format=VK_FORMAT_R32G32B32A32_SFLOAT;io.images[1].format=VK_FORMAT_R32G32_SFLOAT;io.images[2].format=VK_FORMAT_R32_SFLOAT;io.images[4].format=VK_FORMAT_R32G32B32A32_SFLOAT;
    auto setup=[&](NRSize render,uint32_t height){
        NRSize network=nr_network_size(render,height);b.geometry=nr_geometry(render,network,render);b.plan=original;reshape_plan(b.plan,b.geometry);share_production_inputs(b.plan,b.geometry);route_production_history(b.plan,b.geometry,argv[1]);b.history_bank=0;
#ifdef HOST_RECORD_OPTIMIZED
        init_clear_passes(&b);
#endif
        b.pipelines.resize(b.plan.passes.size());for(size_t i=0;i<b.plan.passes.size();++i){b.plan.passes[i].pipeline=i;b.plan.passes[i].set=(VkDescriptorSet)(uintptr_t)(3000+i);b.pipelines[i].handle=(VkPipeline)(uintptr_t)(1000+i);b.pipelines[i].layout=(VkPipelineLayout)(uintptr_t)(2000+i);}
        for(size_t i=0;i<b.plan.buffers.size();++i){auto& x=b.plan.buffers[i];x.handle=x.production_unused?VK_NULL_HANDLE:(VkBuffer)(uintptr_t)(4000+i);if(x.name==kPreImageName)b.pre=i;if(x.name==kPostImageName)b.post=i;}
        b.output=b.plan.outputs.front().resource;b.resample.handle=(VkBuffer)5001;b.resample.bytes=uint64_t(render.w)*network.h*24;b.filter_tables.handle=(VkBuffer)5002;b.filter_tables.bytes=2048;
        b.history_committed=b.record_pending=false;b.last_network_fence=VK_NULL_HANDLE;
        b.gather.handle=(VkPipeline)6001;b.gather.layout=(VkPipelineLayout)6002;b.gather_set=(VkDescriptorSet)6003;
        b.scatter.handle=(VkPipeline)6011;b.scatter.layout=(VkPipelineLayout)6012;b.scatter32=b.scatter;b.scatter_set=(VkDescriptorSet)6013;
        b.low_gather=b.gather;b.low_gather_set=b.gather_set;b.low_scatter=b.scatter;b.low_scatter32=b.scatter;b.low_scatter_set=b.scatter_set;
        params.render_width=render.w;params.render_height=render.h;params.output_width=render.w;params.output_height=render.h;params.reset_history=0;
        io.nrb_controls.input_height=height;io.model_controls[9]=float(network.w);io.model_controls[10]=float(network.h);io.frame_id=0;io.control_epoch=1;io.nrb_controls.history=NRB_HISTORY_REFERENCE;
        for(auto& im:io.images){im.extent={render.w,render.h};im.valid_rect={{0,0},{render.w,render.h}};}
    };
    VkCommandBuffer cb=(VkCommandBuffer)1;
    if(std::string(argv[2])=="trace"){
        tracing=true;trace_file.open(argv[3]);require(bool(trace_file),"trace output");
        std::vector<std::pair<NRSize,uint32_t>> cases;
        for(auto size:{NRSize{1280,720},NRSize{640,360},NRSize{853,480},NRSize{854,480},NRSize{960,540},NRSize{1114,626},NRSize{1485,835},NRSize{1707,960},NRSize{1920,1080},NRSize{2227,1253},NRSize{2560,1440},NRSize{3840,2160}})cases.push_back({size,0});
        for(auto test:std::vector<std::pair<NRSize,uint32_t>>{{{1280,720},360},{{1920,1080},480},{{1707,960},540},{{2560,1440},720},{{3840,2160},720}})cases.push_back(test);
        for(auto test:cases){setup(test.first,test.second);event({100,test.first.w,test.first.h,test.second});
            const auto immutable=b.plan;
            for(uint32_t frame=0;frame<7;++frame){io.frame_id=frame;io.noise_seed=frame*137+19;params.reset_history=frame==2;io.control_epoch=frame>=3?2:1;io.nrb_controls.history=frame==4?NRB_HISTORY_ZERO_MOTION:frame==5?NRB_HISTORY_RESET:NRB_HISTORY_REFERENCE;io.images[2].image=frame%2?(VkImage)3:VK_NULL_HANDLE;views[2]=frame%2?(VkImageView)3:VK_NULL_HANDLE;io.images[4].format=frame%2?VK_FORMAT_R16G16B16A16_SFLOAT:VK_FORMAT_R32G32B32A32_SFLOAT;
                event({101,frame});require(nrvk_backend_frame(&b,cb,&io.v3)==0,"record trace");require(port_network_bridge_backend_submit_result(&b,(VkFence)1,VK_SUCCESS)==0,"trace submit");
                for(size_t i=0;i<b.plan.passes.size();++i)require(b.plan.passes[i].push==immutable.passes[i].push,"immutable pass push");
            }
        }
        std::cout<<"COMMAND_TRACE_PASS cases="<<cases.size()<<" frames="<<cases.size()*7<<" real production reshape/share; full command arguments and payloads; no GPU\n";
    }else{
        require(argc==6,"bench warm measure");setup({1280,720},720);io.images[2].image=(VkImage)3;views[2]=(VkImageView)3;
        unsigned warm=std::stoul(argv[4]),samples=std::stoul(argv[5]);LARGE_INTEGER frequency{},start{},end{};QueryPerformanceFrequency(&frequency);std::ofstream csv(argv[3]);require(bool(csv),"bench output");csv<<"frame,warmup,record_ms\n"<<std::setprecision(12);
        for(unsigned i=0;i<warm+samples;++i){io.frame_id=i;io.noise_seed=i;QueryPerformanceCounter(&start);int rc=nrvk_backend_frame(&b,cb,&io.v3);QueryPerformanceCounter(&end);require(rc==0,"bench record");require(port_network_bridge_backend_submit_result(&b,(VkFence)1,VK_SUCCESS)==0,"bench submit");csv<<i<<','<<(i<warm)<<','<<1000.0*double(end.QuadPart-start.QuadPart)/double(frequency.QuadPart)<<'\n';}
        std::cout<<"CPU_RECORD_BENCH_PASS warm="<<warm<<" samples="<<samples<<" frequency="<<frequency.QuadPart<<" observed="<<observed<<"; QPC around actual nrvk_backend_frame only; spy fence success; no loader/GPU/wait/submit/CSV inside measured region\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" last_error="<<port_network_bridge_backend_last_error()<<'\n';return 1;}}
