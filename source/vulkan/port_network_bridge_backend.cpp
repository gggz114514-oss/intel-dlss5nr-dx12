// External-device adapter extracted from the frozen basicVMM model runner.
// No frame submit, external-image import, device creation or game installation.
#define VK_NO_PROTOTYPES
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "port_network_bridge_backend.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "port_network_bridge_backend_model_identity.inc"
#include "port_network_bridge_backend_plan.inc"
#include "nr_geometry.h"
#include "nr_resample.h"
#include "nr_network_shape.inc"
#include "port_network_bridge_backend_frozen.inc"

static thread_local std::string last_error;
extern "C" const char* port_network_bridge_backend_last_error() { return last_error.c_str(); }
#define CHECK(x) do { const VkResult port_bridge_result_=(x); if(port_bridge_result_!=VK_SUCCESS) throw std::runtime_error(std::string(#x)+" VkResult="+std::to_string(port_bridge_result_)); } while(0)
#define DEVICE_FUNCTIONS(F) \
 F(vkCreateCommandPool) F(vkDestroyCommandPool) F(vkAllocateCommandBuffers) \
 F(vkResetCommandBuffer) F(vkBeginCommandBuffer) F(vkEndCommandBuffer) \
 F(vkCreateBuffer) F(vkDestroyBuffer) F(vkGetBufferMemoryRequirements2) \
 F(vkAllocateMemory) F(vkFreeMemory) F(vkBindBufferMemory) F(vkMapMemory) F(vkUnmapMemory) \
 F(vkCreateDescriptorSetLayout) F(vkDestroyDescriptorSetLayout) F(vkCreateDescriptorPool) \
 F(vkDestroyDescriptorPool) F(vkAllocateDescriptorSets) F(vkUpdateDescriptorSets) \
 F(vkCreateShaderModule) F(vkDestroyShaderModule) F(vkCreatePipelineLayout) \
 F(vkDestroyPipelineLayout) F(vkCreateComputePipelines) F(vkDestroyPipeline) \
 F(vkCmdBindPipeline) F(vkCmdBindDescriptorSets) F(vkCmdPushConstants) F(vkCmdDispatch) \
 F(vkCmdFillBuffer) F(vkCmdUpdateBuffer) F(vkCmdCopyBuffer) F(vkCmdPipelineBarrier) \
 F(vkCreateFence) F(vkDestroyFence) F(vkResetFences) F(vkWaitForFences) F(vkGetFenceStatus) \
 F(vkQueueSubmit) F(vkQueueWaitIdle) F(vkCreateSampler) F(vkDestroySampler)
struct NRVK_Backend {
    std::string manifest_dir;
    HMODULE loader=nullptr;
    VkDevice device=VK_NULL_HANDLE; VkQueue queue=VK_NULL_HANDLE; uint32_t family=0;
    Plan plan; VkPhysicalDeviceMemoryProperties memory{};
    NRGeometry geometry=nr_geometry({1280,720},{1280,720},{1280,720});
    std::vector<VkDeviceMemory> pool_memory; std::vector<Pipeline> pipelines;
    VkDescriptorPool descriptors=VK_NULL_HANDLE;
    Pipeline gather{},scatter{},scatter32{}; VkDescriptorSet gather_set=VK_NULL_HANDLE,scatter_set=VK_NULL_HANDLE;
    Pipeline low_gather{},low_scatter{},low_scatter32{};VkDescriptorSet low_gather_set=VK_NULL_HANDLE,low_scatter_set=VK_NULL_HANDLE;
    Buffer resample,filter_tables;
    NRFilterAxis filter_axes[6]{};std::vector<uint32_t> filter_words;
    VkSampler sampler=VK_NULL_HANDLE;
    VkCommandPool startup_pool=VK_NULL_HANDLE; VkCommandBuffer startup_cb=VK_NULL_HANDLE;
    VkFence startup_fence=VK_NULL_HANDLE;
    Buffer staging; void* mapped=nullptr;
    size_t pre=0,post=0,output=0;
    bool record_pending=false,history_committed=false;
    uint64_t pending_frame=0,pending_epoch=0,committed_frame=0,committed_epoch=0;
    VkFence last_network_fence=VK_NULL_HANDLE; VkQueryPool ts_query_pool=VK_NULL_HANDLE; uint32_t ts_base_query=0; PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp=nullptr;
#define FN(f) PFN_##f f=nullptr;
    DEVICE_FUNCTIONS(FN)
#undef FN
};

static std::string sha256(const std::filesystem::path& path) {
    auto bytes=read_bytes(path.string()); BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 provider");
    unsigned char digest[32];
    NTSTATUS rc=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
    if(rc>=0)rc=BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0);
    if(rc>=0)rc=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    if(rc<0)throw std::runtime_error("SHA256 failed");
    std::ostringstream s;for(auto x:digest)s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(x);return s.str();
}
static Plan checked_plan(const char* directory) {
    if(!directory)throw std::runtime_error("manifest_dir missing");
    auto dir=std::filesystem::path(directory);
    if(sha256(dir/"port_network_manifest.json")!=frozen_manifest_sha ||
       sha256(dir/"port_network_runtime.txt")!=frozen_runtime_sha)
        throw std::runtime_error("requires exact build-frozen manifest/runtime (backend-adapter/port_queue_backend_frozen.json)");
    for(const auto& f:frozen_files)if(sha256(dir/f.path)!=f.sha)throw std::runtime_error(std::string("frozen file hash: ")+f.path);
    Plan p=read_plan((dir/"port_network_runtime.txt").string());
    if(p.passes.size()!=kPlanPasses || p.outputs.size()!=kPlanOutputs || p.history.size()!=kPlanHistory || p.buffers.size()!=kPlanBuffers)
        throw std::runtime_error("prepared plan dimensions differ from this build profile");
    return p;
}
extern "C" int nrvk_backend_probe(const char* dir) {
    if(!dir){last_error="manifest_dir missing";return -1;}
    try {auto p=checked_plan(dir);last_error.clear();return 0;}
    catch(const std::exception& e){last_error=e.what();return -2;}
}
static void barrier(NRVK_Backend* b,VkCommandBuffer cb,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags a,VkAccessFlags z) {
    VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};m.srcAccessMask=a;m.dstAccessMask=z;
    b->vkCmdPipelineBarrier(cb,src,dst,0,1,&m,0,nullptr,0,nullptr);
}
static void make_buffer(NRVK_Backend* b,Buffer& buf,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags) {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=buf.bytes;ci.usage=usage;
    CHECK(b->vkCreateBuffer(b->device,&ci,nullptr,&buf.handle));
    VkMemoryDedicatedRequirements dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 req{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};req.pNext=&dedicated;
    VkBufferMemoryRequirementsInfo2 query{VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2};query.buffer=buf.handle;
    b->vkGetBufferMemoryRequirements2(b->device,&query,&req);
    const auto& need=req.memoryRequirements;uint32_t type=0;
    while(type<b->memory.memoryTypeCount && !((need.memoryTypeBits&(1u<<type)) &&
        (b->memory.memoryTypes[type].propertyFlags&flags)==flags))++type;
    if(type==b->memory.memoryTypeCount)throw std::runtime_error("required memory type: "+buf.name);
    buf.memoryType=type;buf.allocated=need.size;buf.alignment=need.alignment;buf.typeBits=need.memoryTypeBits;
    buf.dedicatedRequired=dedicated.requiresDedicatedAllocation;buf.dedicatedPreferred=dedicated.prefersDedicatedAllocation;
    if((flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)||buf.dedicatedRequired) {
        VkMemoryDedicatedAllocateInfo di{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};di.buffer=buf.handle;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=need.size;ai.memoryTypeIndex=type;ai.pNext=buf.dedicatedRequired?&di:nullptr;
        CHECK(b->vkAllocateMemory(b->device,&ai,nullptr,&buf.memory));CHECK(b->vkBindBufferMemory(b->device,buf.handle,buf.memory,0));
    }
}
static void pool_buffers(NRVK_Backend* b) {
    struct Slot {VkDeviceSize offset,size;uint32_t last;};
    std::map<std::pair<uint32_t,bool>,std::vector<size_t>> groups;
    for(size_t i=0;i<b->plan.buffers.size();++i){auto& x=b->plan.buffers[i];if(!x.memory)groups[{x.memoryType,x.resident}].push_back(i);}
    for(auto& group:groups) {
        auto ids=group.second;std::sort(ids.begin(),ids.end(),[&](size_t a,size_t c){return b->plan.buffers[a].firstPass<b->plan.buffers[c].firstPass;});
        VkDeviceSize size=0;std::vector<Slot> slots;
        for(auto id:ids) {
            auto& x=b->plan.buffers[id];Slot* selected=nullptr;
            // Small's tested B580 pool-layout workaround: preserve the Vulkan requirement
            // and page-align medium-small buffers. No per-frame work is added.
            VkDeviceSize slotAlign=x.alignment;
            if(x.allocated>4096 && x.allocated<=65536 && slotAlign<4096)slotAlign=4096;
            if(!x.resident)for(auto& s:slots)if(s.last<x.firstPass && s.size>=x.allocated && s.offset%slotAlign==0)
                if(!selected||s.size<selected->size)selected=&s;
            if(selected){x.memoryOffset=selected->offset;selected->last=x.lastPass;}
            else{x.memoryOffset=(size+slotAlign-1)/slotAlign*slotAlign;slots.push_back({x.memoryOffset,x.allocated,x.lastPass});size=x.memoryOffset+x.allocated;}
            x.ownsMemory=false;
        }
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=size;ai.memoryTypeIndex=group.first.first;
        VkDeviceMemory mem;CHECK(b->vkAllocateMemory(b->device,&ai,nullptr,&mem));b->pool_memory.push_back(mem);
        for(auto id:ids){auto& x=b->plan.buffers[id];x.memory=mem;CHECK(b->vkBindBufferMemory(b->device,x.handle,mem,x.memoryOffset));}
    }
}
static Pipeline pipeline(NRVK_Backend* b,const std::string& spv,const std::vector<VkDescriptorSetLayoutBinding>& bindings,uint32_t push_bytes,uint32_t subgroup,bool spatial_tail=false,bool scalar_post=false) {
    Pipeline p{};
    try {
    VkDescriptorSetLayoutCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};di.bindingCount=uint32_t(bindings.size());di.pBindings=bindings.data();
    CHECK(b->vkCreateDescriptorSetLayout(b->device,&di,nullptr,&p.descriptors));
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,push_bytes};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&p.descriptors;li.pushConstantRangeCount=push_bytes?1:0;li.pPushConstantRanges=&range;
    CHECK(b->vkCreatePipelineLayout(b->device,&li,nullptr,&p.layout));
    auto bytes=read_bytes(spv);std::vector<uint32_t> words(bytes.size()/4);memcpy(words.data(),bytes.data(),bytes.size());
    // The accepted fused post-body has exactly three references to the source
    // dimensions (x bound, y bound, raster stride). Change only its OpConstant
    // literals after the frozen file has been checked. IDs and NoContraction
    // decorations stay untouched. Native720 uses the unmodified frozen module.
    if(spatial_tail)reshape_tail_spirv(words,b->geometry.network);
    if(scalar_post)reshape_post_local_size(words,b->geometry.network);
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=bytes.size();mi.pCode=words.data();VkShaderModule module;
    CHECK(b->vkCreateShaderModule(b->device,&mi,nullptr,&module));
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo sg{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};sg.requiredSubgroupSize=subgroup;
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.pNext=&sg;ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="main";ci.layout=p.layout;
    VkResult r=b->vkCreateComputePipelines(b->device,VK_NULL_HANDLE,1,&ci,nullptr,&p.handle);b->vkDestroyShaderModule(b->device,module,nullptr);CHECK(r);return p;
    } catch(...) {
        if(p.handle)b->vkDestroyPipeline(b->device,p.handle,nullptr);
        if(p.layout)b->vkDestroyPipelineLayout(b->device,p.layout,nullptr);
        if(p.descriptors)b->vkDestroyDescriptorSetLayout(b->device,p.descriptors,nullptr);
        throw;
    }
}
static VkDescriptorSet allocate_set(NRVK_Backend* b,VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=b->descriptors;ai.descriptorSetCount=1;ai.pSetLayouts=&layout;
    VkDescriptorSet set;CHECK(b->vkAllocateDescriptorSets(b->device,&ai,&set));return set;
}
static void init_resources(NRVK_Backend* b) {
    for(auto& x:b->plan.buffers){make_buffer(b,x,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);b->staging.bytes=std::max(b->staging.bytes,x.bytes);}
    pool_buffers(b);
    if(b->geometry.render!=b->geometry.network){
        b->resample.name="resample_separable";b->resample.bytes=uint64_t(b->geometry.render.w)*b->geometry.network.h*24;
        make_buffer(b,b->resample,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        // Scratch is intentionally outside the network alias pool: both resize
        // phases and residual recovery consume it on opposite sides of NR.
        if(!b->resample.memory){VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=b->resample.allocated;ai.memoryTypeIndex=b->resample.memoryType;CHECK(b->vkAllocateMemory(b->device,&ai,nullptr,&b->resample.memory));CHECK(b->vkBindBufferMemory(b->device,b->resample.handle,b->resample.memory,0));}
        auto& g=b->geometry;auto& words=b->filter_words;
        b->filter_axes[0]=nr_filter_axis(words,g.render.h,g.network.h,0);b->filter_axes[1]=nr_filter_axis(words,g.render.w,g.network.w,0);
        b->filter_axes[2]=nr_filter_axis(words,g.render.h,g.network.h,1);b->filter_axes[3]=nr_filter_axis(words,g.render.w,g.network.w,1);
        b->filter_axes[4]=nr_filter_axis(words,g.network.w,g.render.w,2);b->filter_axes[5]=nr_filter_axis(words,g.network.h,g.render.h,2);
        auto& t=b->filter_tables;t.name="resample_filter_tables";t.bytes=words.size()*4;
        make_buffer(b,t,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if(!t.memory){VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=t.allocated;ai.memoryTypeIndex=t.memoryType;CHECK(b->vkAllocateMemory(b->device,&ai,nullptr,&t.memory));CHECK(b->vkBindBufferMemory(b->device,t.handle,t.memory,0));}
        b->staging.bytes=std::max(b->staging.bytes,t.bytes);
    }
    b->staging.name="startup_staging";make_buffer(b,b->staging,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(b->vkMapMemory(b->device,b->staging.memory,0,VK_WHOLE_SIZE,0,&b->mapped));
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=b->family;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CHECK(b->vkCreateCommandPool(b->device,&cp,nullptr,&b->startup_pool));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=b->startup_pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
    CHECK(b->vkAllocateCommandBuffers(b->device,&ca,&b->startup_cb));VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};CHECK(b->vkCreateFence(b->device,&fc,nullptr,&b->startup_fence));
    std::vector<Buffer*> startup;for(auto& x:b->plan.buffers)startup.push_back(&x);if(b->filter_tables.handle)startup.push_back(&b->filter_tables);
    for(auto* ptr:startup) {auto& x=*ptr;
        if(!x.resident&&!x.dedicatedRequired)continue;
        if(ptr==&b->filter_tables)memcpy(b->mapped,b->filter_words.data(),size_t(x.bytes));
        else if(x.path=="-")memset(b->mapped,0,size_t(x.bytes));
        else{auto data=read_bytes(x.path);if(data.size()!=x.bytes)throw std::runtime_error("cold size: "+x.name);memcpy(b->mapped,data.data(),data.size());}
        CHECK(b->vkResetCommandBuffer(b->startup_cb,0));VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK(b->vkBeginCommandBuffer(b->startup_cb,&bi));VkBufferCopy copy{0,0,x.bytes};b->vkCmdCopyBuffer(b->startup_cb,b->staging.handle,x.handle,1,&copy);
        barrier(b,b->startup_cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        CHECK(b->vkEndCommandBuffer(b->startup_cb));CHECK(b->vkResetFences(b->device,1,&b->startup_fence));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&b->startup_cb;CHECK(b->vkQueueSubmit(b->queue,1,&si,b->startup_fence));
        CHECK(b->vkWaitForFences(b->device,1,&b->startup_fence,VK_TRUE,60ULL*1000*1000*1000));
    }
    b->vkUnmapMemory(b->device,b->staging.memory);b->mapped=nullptr;
    b->vkDestroyBuffer(b->device,b->staging.handle,nullptr);b->staging.handle=VK_NULL_HANDLE;
    b->vkFreeMemory(b->device,b->staging.memory,nullptr);b->staging.memory=VK_NULL_HANDLE;
    b->vkDestroyFence(b->device,b->startup_fence,nullptr);b->startup_fence=VK_NULL_HANDLE;
    b->vkDestroyCommandPool(b->device,b->startup_pool,nullptr);b->startup_pool=VK_NULL_HANDLE;b->startup_cb=VK_NULL_HANDLE;
    std::map<std::string,size_t> cache;uint32_t count=11;
    for(auto& pass:b->plan.passes) {
        count+=uint32_t(pass.bindings.size());std::string key=pass.spv+":"+std::to_string(pass.push.size())+":"+std::to_string(pass.subgroup);
        for(auto& bind:pass.bindings)key+=":"+std::to_string(bind.binding);
        if(cache.count(key)){pass.pipeline=cache[key];continue;}
        std::vector<VkDescriptorSetLayoutBinding> bindings;for(auto& bind:pass.bindings)bindings.push_back({bind.binding,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        pass.pipeline=b->pipelines.size();cache[key]=pass.pipeline;b->pipelines.push_back(pipeline(b,pass.spv,bindings,uint32_t(pass.push.size()*4),pass.subgroup,pass.name.find("s37_")!=std::string::npos,&pass==&b->plan.passes.back()));
    }
    b->gather=pipeline(b,(std::filesystem::path(b->manifest_dir)/gather_spv).string(),{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},64,32);
    b->scatter=pipeline(b,(std::filesystem::path(b->manifest_dir)/scatter_spv).string(),{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},16,32);
    b->scatter32=pipeline(b,(std::filesystem::path(b->manifest_dir)/scatter32_spv).string(),{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},16,32);
    if(b->resample.handle){
        b->low_gather=pipeline(b,(std::filesystem::path(b->manifest_dir)/low_gather_spv).string(),{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{6,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},112,32);
        std::vector<VkDescriptorSetLayoutBinding> bindings={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        bindings.push_back({5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        b->low_scatter=pipeline(b,(std::filesystem::path(b->manifest_dir)/low_scatter_spv).string(),bindings,64,32);
        b->low_scatter32=pipeline(b,(std::filesystem::path(b->manifest_dir)/low_scatter32_spv).string(),bindings,64,32);
    }
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,7},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}};
    VkDescriptorPoolCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};di.maxSets=uint32_t(b->plan.passes.size()+4);di.poolSizeCount=3;di.pPoolSizes=sizes;CHECK(b->vkCreateDescriptorPool(b->device,&di,nullptr,&b->descriptors));
    for(auto& pass:b->plan.passes) {
        pass.set=allocate_set(b,b->pipelines[pass.pipeline].descriptors);std::vector<VkDescriptorBufferInfo> info(pass.bindings.size());std::vector<VkWriteDescriptorSet> writes(pass.bindings.size());
        for(size_t i=0;i<pass.bindings.size();++i){auto& x=pass.bindings[i];info[i]={b->plan.buffers[x.resource].handle,x.offset,x.bytes};writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=pass.set;writes[i].dstBinding=x.binding;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&info[i];}
        b->vkUpdateDescriptorSets(b->device,uint32_t(writes.size()),writes.data(),0,nullptr);
    }
    b->gather_set=allocate_set(b,b->gather.descriptors);b->scatter_set=allocate_set(b,b->scatter.descriptors);
    if(b->resample.handle){b->low_gather_set=allocate_set(b,b->low_gather.descriptors);b->low_scatter_set=allocate_set(b,b->low_scatter.descriptors);}
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=VK_FILTER_NEAREST;sc.minFilter=VK_FILTER_NEAREST;sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sc.maxLod=0;
    CHECK(b->vkCreateSampler(b->device,&sc,nullptr,&b->sampler));
    // 102's first pass is the F1 transport. Binding 1 is its aux pack, not the
    // image bundle. Select the frozen IO bundles by their semantic names.
    bool found_pre=false,found_post=false;
    for(size_t i=0;i<b->plan.buffers.size();++i) {
        if(b->plan.buffers[i].name==kPreImageName){b->pre=i;found_pre=true;}
        if(b->plan.buffers[i].name==kPostImageName){b->post=i;found_post=true;}
    }
    if(!found_pre||!found_post)throw std::runtime_error("Small ioport image bundles missing");
    if(b->plan.buffers[b->pre].bytes!=b->geometry.pre_bytes || b->plan.buffers[b->post].bytes!=b->geometry.post_bytes)
        throw std::runtime_error("102 ioport bundle size mismatch");
    b->output=b->plan.outputs.front().resource;
    if(b->plan.buffers[b->output].bytes!=b->geometry.frame_bytes)throw std::runtime_error("frame_out geometry size mismatch");
}

int nrvk_backend_init_geometry(VkInstance instance,VkDevice device,VkPhysicalDevice phy,VkQueue queue,uint32_t family,const NRVK_Config* cfg,NRSize render,NRSize network,NRVK_Backend** out) {
    if(!out||!cfg||cfg->abi_size!=sizeof(*cfg)||!instance||!device||!phy||!queue){last_error="init external device/config missing";return -1;}*out=nullptr;
    NRVK_Backend* b=nullptr;int failure=-2;
    try {
        auto plan=checked_plan(cfg->manifest_dir);b=new NRVK_Backend();b->geometry=nr_geometry(render,network,render);reshape_plan(plan,b->geometry);b->manifest_dir=cfg->manifest_dir;b->plan=std::move(plan);b->device=device;b->queue=queue;b->family=family;
        failure=-4;b->loader=LoadLibraryA("vulkan-1.dll");if(!b->loader)throw std::runtime_error("vulkan-1.dll missing");
        auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(b->loader,"vkGetInstanceProcAddr"));
        auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"));
#define LOAD(f) b->f=reinterpret_cast<PFN_##f>(gdpa(device,#f));if(!b->f)throw std::runtime_error("missing device function " #f);
        DEVICE_FUNCTIONS(LOAD) b->vkCmdWriteTimestamp=reinterpret_cast<PFN_vkCmdWriteTimestamp>(gdpa(device,"vkCmdWriteTimestamp"));
#undef LOAD
        failure=-3;auto props=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance,"vkGetPhysicalDeviceProperties2"));
        VkPhysicalDeviceProperties2 physical{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props(phy,&physical);
        if(physical.properties.vendorID!=0x8086||physical.properties.deviceID!=0xe20b||physical.properties.limits.maxComputeSharedMemorySize<49152)throw std::runtime_error("expected B580/shared48KiB");
        const auto& limits=physical.properties.limits;
        for(const auto& pass:b->plan.passes)if(pass.gx>limits.maxComputeWorkGroupCount[0]||pass.gy>limits.maxComputeWorkGroupCount[1]||pass.gz>limits.maxComputeWorkGroupCount[2])throw std::runtime_error("geometry dispatch exceeds device limits: "+pass.name);
        for(const auto& buffer:b->plan.buffers)if(buffer.bytes>limits.maxStorageBufferRange)throw std::runtime_error("geometry storage range exceeds device limit: "+buffer.name);
        uint64_t bridge_pixels=std::max(render.pixels(),std::max(network.pixels(),uint64_t(render.w)*network.h));
        if((bridge_pixels+255)/256>limits.maxComputeWorkGroupCount[0])throw std::runtime_error("geometry bridge dispatch exceeds device limit");
        failure=-4;auto features_fn=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(gipa(instance,"vkGetPhysicalDeviceFeatures2"));
        VkPhysicalDeviceCooperativeMatrixFeaturesKHR cm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
        VkPhysicalDeviceShaderFloat16Int8Features half{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
        VkPhysicalDevice16BitStorageFeatures storage{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
        VkPhysicalDeviceSubgroupSizeControlFeatures sg{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
        VkPhysicalDeviceSynchronization2Features sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        VkPhysicalDeviceVulkanMemoryModelFeatures mm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&cm;cm.pNext=&half;half.pNext=&storage;storage.pNext=&sg;sg.pNext=&sync;sync.pNext=&mm;features_fn(phy,&features);
        if(!cm.cooperativeMatrix||!half.shaderFloat16||!storage.storageBuffer16BitAccess||!sg.subgroupSizeControl||!sg.computeFullSubgroups||!sync.synchronization2||!mm.vulkanMemoryModel)throw std::runtime_error("required FP16/SG32/coop/basicVMM support missing");
        auto getmem=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));getmem(phy,&b->memory);
        failure=-8;init_resources(b);last_error.clear();*out=b;return 0;
    } catch(const std::exception& e) {last_error=e.what();nrvk_backend_release(b);return failure;}
}
extern "C" int nrvk_backend_init(VkInstance instance,VkDevice device,VkPhysicalDevice phy,VkQueue queue,uint32_t family,const NRVK_Config* cfg,NRVK_Backend** out){return nrvk_backend_init_geometry(instance,device,phy,queue,family,cfg,{1280,720},{1280,720},out);}

static bool crop_valid(const PortNetworkBridgeImage& x,uint32_t bx,uint32_t by,NRSize size) {
    return x.image && x.layout==VK_IMAGE_LAYOUT_GENERAL && x.valid_rect.offset.x>=0 && x.valid_rect.offset.y>=0 &&
        bx>=uint32_t(x.valid_rect.offset.x)&&by>=uint32_t(x.valid_rect.offset.y)&&
        uint64_t(bx)+size.w<=uint64_t(x.valid_rect.offset.x)+x.valid_rect.extent.width&&
        uint64_t(by)+size.h<=uint64_t(x.valid_rect.offset.y)+x.valid_rect.extent.height&&
        uint64_t(x.valid_rect.offset.x)+x.valid_rect.extent.width<=x.extent.width&&
        uint64_t(x.valid_rect.offset.y)+x.valid_rect.extent.height<=x.extent.height;
}
static bool frame_supported(NRVK_Backend* b,const PortNetworkBridgeFrameIO& io) {
    const auto& p=*io.v3.params;
    // output_width/height describe the later SR target, not the NR replacement.
    // MVLowRes is native render resolution relative to that SR target.
    NRSize render{p.render_width,p.render_height};
    const uint32_t height=io.nrb_controls.input_height;
    if(height!=0&&height!=360&&height!=480&&height!=540&&height!=720)return false;
    if(p.abi_size!=sizeof(p)||!render.w||!render.h||render!=b->geometry.render||nr_network_size(render,io.nrb_controls.input_height)!=b->geometry.network||p.output_width<render.w||p.output_height<render.h||
       (!p.low_resolution_motion&&(p.output_width!=render.w||p.output_height!=render.h))||
       p.hdr_input||p.auto_exposure||p.pre_exposure!=1.0f||p.exposure_scale!=1.0f||p.motion_jittered||p.jitter_x!=0.0f||p.jitter_y!=0.0f||
       p.motion_scale_origin<NRB_MV_DLSS_NGX||p.motion_scale_origin>NRB_MV_XESS_TO_NGX)return false;
    uint32_t bx[]={p.color_base_x,p.motion_base_x,p.depth_base_x,p.output_base_x};uint32_t by[]={p.color_base_y,p.motion_base_y,p.depth_base_y,p.output_base_y};
    for(unsigned i=0;i<4;++i){unsigned index=i==3?4:i;const auto& x=io.images[index];
        if(i==2&&!x.image)continue; // No depth: original have_depth=false branch.
        if(!crop_valid(x,bx[i],by[i],render)||x.queue_family!=b->family||!(x.usage&(i==3?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT)))return false;}
    auto c=io.images[0].format,m=io.images[1].format,d=io.images[2].format;
    return (c==VK_FORMAT_R16G16B16A16_SFLOAT||c==VK_FORMAT_R32G32B32A32_SFLOAT)&&
        (m==VK_FORMAT_R16G16_SFLOAT||m==VK_FORMAT_R32G32_SFLOAT)&&(!io.images[2].image||d==VK_FORMAT_R32_SFLOAT||d==VK_FORMAT_D32_SFLOAT)&&
        (io.images[4].format==VK_FORMAT_R16G16B16A16_SFLOAT||io.images[4].format==VK_FORMAT_R32G32B32A32_SFLOAT)&&io.nrb_controls.abi_size==sizeof(NRB_Controls)&&
        io.model_controls[9]==float(b->geometry.network.w)&&io.model_controls[10]==float(b->geometry.network.h);
}
extern "C" void nrvk_backend_set_timestamp_query(NRVK_Backend* b,VkQueryPool pool,uint32_t base_query) { if(!b)return; b->ts_query_pool=pool; b->ts_base_query=base_query; }
extern "C" int nrvk_backend_frame(NRVK_Backend* b,VkCommandBuffer cb,const NRVK_BackendFrameIO* base) {
    if(!b||!cb||!base||!base->params){last_error="frame context/cb/params missing";return -1;}
    if(base->abi_size!=sizeof(PortNetworkBridgeFrameIO)){last_error="v3 lacks actual image metadata/frame_id/control_epoch/controls20: use independent PortNetworkBridgeFrameIO tail";return -6;}
    if(base->io_view_count!=4||!base->io_views||!base->io_views[0]||!base->io_views[1]||!base->out_view){last_error="color/motion/output views missing";return -1;}
    const auto& io=*reinterpret_cast<const PortNetworkBridgeFrameIO*>(base);
    if(!frame_supported(b,io)){last_error="requires matching R/N geometry, float inputs/RGBA16F or32F output, GENERAL crops, normalized LDR proxy(exposure1,jitter0,non-jittered native MV), known NGX motion origin";return -6;}
    if(io.images[2].image&&!base->io_views[2]){last_error="actual depth image requires its sampled view";return -1;}
    if(b->record_pending || (b->last_network_fence && b->vkGetFenceStatus(b->device,b->last_network_fence)!=VK_SUCCESS)){last_error="prior single-context network not completed or record not submitted";return -7;}
    const bool have_depth=io.images[2].image!=VK_NULL_HANDLE;
    const auto& g=b->geometry;const bool low=g.render!=g.network;
    VkDescriptorImageInfo images[4]={{b->sampler,base->io_views[0],VK_IMAGE_LAYOUT_GENERAL},{b->sampler,base->io_views[1],VK_IMAGE_LAYOUT_GENERAL},{b->sampler,have_depth?base->io_views[2]:base->io_views[0],VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,base->out_view,VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo buffers[3]={{b->plan.buffers[b->pre].handle,0,b->plan.buffers[b->pre].bytes},{b->plan.buffers[b->post].handle,0,b->plan.buffers[b->post].bytes},{b->plan.buffers[b->output].handle,0,b->plan.buffers[b->output].bytes}};
    VkWriteDescriptorSet writes[7]{};
    for(unsigned i=0;i<7;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.descriptorCount=1;
        if(i<3){w.dstSet=b->gather_set;w.dstBinding=i;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&images[i];}
        else if(i<5){w.dstSet=b->gather_set;w.dstBinding=i;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&buffers[i-3];}
        else if(i==5){w.dstSet=b->scatter_set;w.dstBinding=0;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&buffers[2];}
        else{w.dstSet=b->scatter_set;w.dstBinding=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[3];}}
    b->vkUpdateDescriptorSets(b->device,7,writes,0,nullptr);
    if(low){
        VkDescriptorBufferInfo scratch{b->resample.handle,0,b->resample.bytes},tables{b->filter_tables.handle,0,b->filter_tables.bytes};VkWriteDescriptorSet extra[13]{};
        for(unsigned i=0;i<13;++i){auto& w=extra[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.descriptorCount=1;
            if(i<7){w.dstSet=b->low_gather_set;w.dstBinding=i;
                if(i<3){w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&images[i];}
                else{w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=i==6?&tables:i==5?&scratch:&buffers[i-3];}}
            else{w.dstSet=b->low_scatter_set;w.dstBinding=i-7;
                if(i==8){w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[3];}
                else if(i==9){w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&images[0];}
                else{w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=i==7?&buffers[2]:i==10?&buffers[0]:i==12?&tables:&scratch;}}
        }
        b->vkUpdateDescriptorSets(b->device,13,extra,0,nullptr);
    }
    auto write_ts=[&](VkPipelineStageFlagBits stage,uint32_t offset){ if(b->ts_query_pool!=VK_NULL_HANDLE&&b->vkCmdWriteTimestamp) b->vkCmdWriteTimestamp(cb,stage,b->ts_query_pool,b->ts_base_query+offset); };
    write_ts(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,1);
    // Global compute execution dependency preserves pool alias WAR ordering
    // across submissions. WB's producer wait/acquire precedes this segment.
    barrier(b,cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    float controls[20];memcpy(controls,io.model_controls,sizeof(controls));
    const auto& p=*base->params;
    // NRB scales are already current->previous source pixels, including route
    // sign/unit normalization. Apply them once; origin is provenance, no flip.
    controls[11]=have_depth?1.0f:0.0f;controls[12]=p.depth_inverted?1.0f:0.0f;
    controls[13]=io.nrb_controls.history==NRB_HISTORY_ZERO_MOTION?0.0f:p.motion_scale_x/float(g.network.w);
    controls[14]=io.nrb_controls.history==NRB_HISTORY_ZERO_MOTION?0.0f:p.motion_scale_y/float(g.network.h);
    controls[15]=controls[16]=1.0f;controls[17]=controls[18]=0.0f;
    const bool history=b->history_committed&&!p.reset_history&&io.nrb_controls.history!=NRB_HISTORY_RESET&&io.control_epoch==b->committed_epoch&&io.frame_id==b->committed_frame+1;
    if(!history)controls[8]=0.0f;
    for(auto& h:b->plan.history){if(history){VkBufferCopy copy{h.sourceOffset,h.destinationOffset,h.bytes};b->vkCmdCopyBuffer(cb,b->plan.buffers[h.source].handle,b->plan.buffers[h.destination].handle,1,&copy);}
        b->vkCmdUpdateBuffer(cb,b->plan.buffers[h.destination].handle,h.controlsOffset,sizeof(controls),controls);}
    barrier(b,cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    write_ts(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,2);
    // Frozen IO-buffer offsets from the ioport contract. These are layout
    // constants, not indices into the unrelated first-pass push constants:
    // color 0, motion 7,372,820 floats, depth 9,216,020 floats; each writes
    // 921,600 pixels of RGBA32F / RG16F / R32F. Post uses the same offsets.
    const auto* f=b->filter_axes;
    uint32_t gather_push[]={p.color_base_x,p.color_base_y,p.motion_base_x,p.motion_base_y,p.depth_base_x,p.depth_base_y,g.network.w,g.network.h,0,g.motion,g.depth,have_depth?1u:0u,0,g.motion,g.depth,0,g.render.w,g.render.h,0,0,f[0].offset,f[0].taps,f[1].offset,f[1].taps,f[2].offset,f[2].taps,f[3].offset,f[3].taps};
    const auto& gather=low?b->low_gather:b->gather;const auto gather_set=low?b->low_gather_set:b->gather_set;
    b->vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,gather.handle);b->vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,gather.layout,0,1,&gather_set,0,nullptr);
    if(low){
        b->vkCmdPushConstants(cb,gather.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(gather_push),gather_push);b->vkCmdDispatch(cb,(g.render.w*g.network.h+255)/256,1,1);
        barrier(b,cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);gather_push[15]=1;
    }
    b->vkCmdPushConstants(cb,gather.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,low?sizeof(gather_push):64,gather_push);b->vkCmdDispatch(cb,uint32_t((g.network.pixels()+255)/256),1,1);
    barrier(b,cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    write_ts(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,3);
    for(size_t i=0;i<b->plan.passes.size();++i){auto& pass=b->plan.passes[i];bool clearing=false;
        for(auto& x:b->plan.buffers)if(!x.resident&&x.firstPass==i&&x.clearFirst){if(!clearing)barrier(b,cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);clearing=true;b->vkCmdFillBuffer(cb,x.handle,0,VK_WHOLE_SIZE,0);}
        if(clearing)barrier(b,cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        auto& pipe=b->pipelines[pass.pipeline];b->vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipe.handle);b->vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipe.layout,0,1,&pass.set,0,nullptr);
        auto push=pass.push;if(i==0){push[22]=io.noise_seed;push[23]&=~4u;}
        if(!push.empty())b->vkCmdPushConstants(cb,pipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,uint32_t(push.size()*4),push.data());b->vkCmdDispatch(cb,pass.gx,pass.gy,pass.gz);
        barrier(b,cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    }
    write_ts(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,4);
    if(low){
        const Pipeline& scatter=io.images[4].format==VK_FORMAT_R32G32B32A32_SFLOAT?b->low_scatter32:b->low_scatter;
        uint32_t push[]={g.network.w,g.network.h,g.render.w,g.render.h,p.color_base_x,p.color_base_y,p.output_base_x,p.output_base_y,0,0,0,0,f[4].offset,f[4].taps,f[5].offset,f[5].taps};
        b->vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,scatter.handle);b->vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,scatter.layout,0,1,&b->low_scatter_set,0,nullptr);
        b->vkCmdPushConstants(cb,scatter.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);b->vkCmdDispatch(cb,(g.render.w*g.network.h+255)/256,1,1);
        barrier(b,cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);push[8]=1;
        b->vkCmdPushConstants(cb,scatter.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);b->vkCmdDispatch(cb,uint32_t((g.render.pixels()+255)/256),1,1);
    }else{
        const Pipeline& scatter=io.images[4].format==VK_FORMAT_R32G32B32A32_SFLOAT?b->scatter32:b->scatter;
        uint32_t scatter_push[]={p.output_base_x,p.output_base_y,g.network.w,g.network.h};b->vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,scatter.handle);b->vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,scatter.layout,0,1,&b->scatter_set,0,nullptr);
        b->vkCmdPushConstants(cb,scatter.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(scatter_push),scatter_push);b->vkCmdDispatch(cb,uint32_t((g.network.pixels()+255)/256),1,1);
    }
    barrier(b,cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    write_ts(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,5);
    b->record_pending=true;b->pending_frame=io.frame_id;b->pending_epoch=io.control_epoch;last_error.clear();return 0;
}
extern "C" int port_network_bridge_backend_submit_result(NRVK_Backend* b,VkFence fence,VkResult result) {
    if(!b||!b->record_pending||(result==VK_SUCCESS&&!fence)){last_error="submit_result requires recorded frame and actual completion fence";return -1;}
    b->record_pending=false;
    if(result==VK_SUCCESS){b->history_committed=true;b->committed_frame=b->pending_frame;b->committed_epoch=b->pending_epoch;b->last_network_fence=fence;}
    return 0;
}
extern "C" void nrvk_backend_release(NRVK_Backend* b) {
    if(!b)return;
    // Caller serializes queue access and resolves/discards unsubmitted CBs.
    // Full queue completion is explicit here; external device/queue stay alive.
    if(b->vkQueueWaitIdle && b->vkQueueWaitIdle(b->queue)!=VK_SUCCESS) {
        last_error="release queue completion failed; context retained, external device owner must resolve completion";
        return;
    }
    if(b->descriptors)b->vkDestroyDescriptorPool(b->device,b->descriptors,nullptr);
    auto free_pipe=[&](Pipeline& p){if(p.handle)b->vkDestroyPipeline(b->device,p.handle,nullptr);if(p.layout)b->vkDestroyPipelineLayout(b->device,p.layout,nullptr);if(p.descriptors)b->vkDestroyDescriptorSetLayout(b->device,p.descriptors,nullptr);};
    for(auto& p:b->pipelines)free_pipe(p);free_pipe(b->gather);free_pipe(b->scatter);free_pipe(b->scatter32);
    free_pipe(b->low_gather);free_pipe(b->low_scatter);free_pipe(b->low_scatter32);
    if(b->resample.handle)b->vkDestroyBuffer(b->device,b->resample.handle,nullptr);if(b->resample.memory)b->vkFreeMemory(b->device,b->resample.memory,nullptr);
    if(b->filter_tables.handle)b->vkDestroyBuffer(b->device,b->filter_tables.handle,nullptr);if(b->filter_tables.memory)b->vkFreeMemory(b->device,b->filter_tables.memory,nullptr);
    if(b->sampler)b->vkDestroySampler(b->device,b->sampler,nullptr);
    if(b->mapped)b->vkUnmapMemory(b->device,b->staging.memory);
    if(b->staging.handle)b->vkDestroyBuffer(b->device,b->staging.handle,nullptr);if(b->staging.memory)b->vkFreeMemory(b->device,b->staging.memory,nullptr);
    for(auto& x:b->plan.buffers){if(x.handle)b->vkDestroyBuffer(b->device,x.handle,nullptr);if(x.memory&&x.ownsMemory)b->vkFreeMemory(b->device,x.memory,nullptr);}
    for(auto mem:b->pool_memory)b->vkFreeMemory(b->device,mem,nullptr);
    if(b->startup_fence)b->vkDestroyFence(b->device,b->startup_fence,nullptr);
    if(b->startup_pool)b->vkDestroyCommandPool(b->device,b->startup_pool,nullptr);
    if(b->loader)FreeLibrary(b->loader);delete b;
}
