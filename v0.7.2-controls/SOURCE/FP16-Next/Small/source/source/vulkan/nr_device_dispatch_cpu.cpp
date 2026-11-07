// Include the actual component macro and context; create no GPU/device.
#ifndef NRVK_CPU_PROCESSOR_SOURCE
#define NRVK_CPU_PROCESSOR_SOURCE "nrb_vulkan_processor.cpp"
#endif
#include NRVK_CPU_PROCESSOR_SOURCE

namespace {
unsigned generation=1,queries=0;
VkResult VKAPI_PTR create_one(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* out) {
    *out=reinterpret_cast<VkSemaphore>(uintptr_t(1));return VK_SUCCESS;
}
VkResult VKAPI_PTR create_two(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* out) {
    *out=reinterpret_cast<VkSemaphore>(uintptr_t(2));return VK_SUCCESS;
}
VkResult VKAPI_PTR create_three(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* out) {
    *out=reinterpret_cast<VkSemaphore>(uintptr_t(3));return VK_SUCCESS;
}
PFN_vkVoidFunction VKAPI_PTR resolve(VkDevice device,const char* name) {
    if(device!=reinterpret_cast<VkDevice>(uintptr_t(generation==2?2:1))||strcmp(name,"vkCreateSemaphore"))return nullptr;
    ++queries;
    return reinterpret_cast<PFN_vkVoidFunction>(generation==1?create_one:generation==2?create_two:create_three);
}
int invoke(Ctx* c) {
    NRVK_DEV_FN(CreateSemaphore);
    VkSemaphore result=VK_NULL_HANDLE;
    if(pfn_CreateSemaphore(c->device,nullptr,nullptr,&result)!=VK_SUCCESS)return -1;
    return int(reinterpret_cast<uintptr_t>(result));
}
}
int main() {
    Ctx c;g_gdpa=resolve;
    for(generation=1;generation<=3;++generation) {
        // Generation3 deliberately reuses generation1's opaque device value.
        c.device=reinterpret_cast<VkDevice>(uintptr_t(generation==2?2:1));
        const int actual=invoke(&c);
        if(actual!=int(generation)) {
            fprintf(stderr,"DEVICE_DISPATCH_STALE generation=%u actual=%d queries=%u\n",generation,actual,queries);return 1;
        }
    }
    if(queries!=3)return 2;
    printf("DEVICE_DISPATCH_CPU_PASS generations=3 queries=3 no GPU\n");return 0;
}
