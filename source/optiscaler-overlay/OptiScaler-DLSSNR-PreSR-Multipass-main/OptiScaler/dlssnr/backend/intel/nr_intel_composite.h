#pragma once
#include "nr_input_contract.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <string>
#include <bit>
#include <cmath>
#include <algorithm>

namespace nrb {
using Microsoft::WRL::ComPtr;
inline void input_barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,
    D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    if(!r||before==after)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
inline void input_uav_barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;list->ResourceBarrier(1,&b);
}
inline ComPtr<ID3D12Resource> input_texture(ID3D12Device* d,UINT w,UINT h,DXGI_FORMAT f,D3D12_RESOURCE_STATES initial=D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=w;desc.Height=h;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
    desc.Format=f;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> r;
    d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,initial,nullptr,IID_PPV_ARGS(&r));
    return r;
}
inline bool input_source_format(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R11G11B10_FLOAT||f==DXGI_FORMAT_R16G16B16A16_FLOAT||
        f==DXGI_FORMAT_R10G10B10A2_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM||
        f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_R32G32B32A32_FLOAT;
}
inline InputStorage input_storage(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R10G10B10A2_UNORM?InputStorage::Unorm10:
        (f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM)?InputStorage::Unorm8:InputStorage::Float;
}
inline bool input_motion_format(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R16G16_FLOAT||f==DXGI_FORMAT_R32G32_FLOAT||
        f==DXGI_FORMAT_R16G16B16A16_FLOAT||f==DXGI_FORMAT_R32G32B32A32_FLOAT;
}
inline bool input_exposure_format(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R16_FLOAT||f==DXGI_FORMAT_R32_FLOAT||input_motion_format(f)||
        f==DXGI_FORMAT_R11G11B10_FLOAT;
}
// Immutable shaders are cached per backend/device. Typed store support is checked
// for the actual destination; BGRA is admitted only on a device supporting it.
struct IntelInputShaders {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> prepare,normalize,composite,composite4;
    std::string error;
    bool Ensure(ID3D12Device* d,const std::wstring& path) {
        if(device.Get()==d&&root&&prepare&&normalize&&composite&&composite4)return true;
        device=d;
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,6,0,0,0},
                                         {D3D12_DESCRIPTOR_RANGE_TYPE_UAV,7,0,0,0}};
        D3D12_ROOT_PARAMETER p[3]{};
        for(UINT i=0;i<2;++i){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[i].DescriptorTable={1,&ranges[i]};}
        p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants={0,0,60};
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;
        ComPtr<ID3DBlob> blob,errors;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors))||
           FAILED(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)))) {
            error="input root signature";return false;
        }
        const auto compile=[&](const char* entry,ComPtr<ID3D12PipelineState>& out) {
            ComPtr<ID3DBlob> code;
            if(FAILED(D3DCompileFromFile(path.c_str(),nullptr,nullptr,entry,"cs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_IEEE_STRICTNESS,0,&code,&errors))) {
                error=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"input shader missing";
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();
            pd.CS={code->GetBufferPointer(),code->GetBufferSize()};
            return SUCCEEDED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&out)));
        };
        return compile("prepare",prepare)&&compile("normalize",normalize)&&
            compile("composite",composite)&&compile("composite_f16",composite4);
    }
};
struct IntelInputConstants {
    uint32_t width,height,sourceX,sourceY,sourceWidth,sourceHeight,motionWidth,motionHeight;
    uint32_t motionX,motionY;float motionScaleX,motionScaleY;
    uint32_t encoding,curve,hdr,feed;
    float divisor,preExposure,exposureScale,strength;
    float shadowFloor,shadowRange,hdrKnee,chromaClamp;
    uint32_t token,useMotion;float attack,release;
    uint32_t governor;float snap,styleSat;uint32_t styleMode;
    float styleTone,styleExposure,styleContrast,styleGain;
    float curveLut[16];
    uint32_t depthWidth,depthHeight,depthX,depthY,useDepth,padding[3];
};
static_assert(sizeof(IntelInputConstants)==60*4);
// Each retained recipe owns proxy, guides, normalization and two immutable heaps.
// Norm retains the pre-execution state; replay never advances the exposure stream.
class IntelInputAdapter {
    ComPtr<ID3D12Resource> proxy_,motion_,depth_,norm_,commit_,source_,gameMotion_,gameDepth_,exposure_,destination_;
    ComPtr<ID3D12DescriptorHeap> heap_;
    IntelInputShaders* shaders_=nullptr;UINT stride_=0;
    IntelInputConstants c_{};DXGI_FORMAT format_=DXGI_FORMAT_UNKNOWN;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT slot) const {
        auto h=heap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(slot)*stride_;return h;
    }
    void srv(UINT slot,ID3D12Resource* r,DXGI_FORMAT f) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=f;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
        shaders_->device->CreateShaderResourceView(r,&d,cpu(slot));
    }
    void uav(UINT slot,ID3D12Resource* r,DXGI_FORMAT f) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=f;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        shaders_->device->CreateUnorderedAccessView(r,nullptr,&d,cpu(slot));
    }
    void Bind(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pso,bool composite=false) {
        ID3D12DescriptorHeap* heaps[]={heap_.Get()};list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootSignature(shaders_->root.Get());
        auto h=heap_->GetGPUDescriptorHandleForHeapStart();if(composite)h.ptr+=13*UINT64(stride_);
        list->SetComputeRootDescriptorTable(0,h);h.ptr+=6*UINT64(stride_);list->SetComputeRootDescriptorTable(1,h);
        list->SetComputeRoot32BitConstants(2,60,&c_,0);list->SetPipelineState(pso);
    }
public:
    ID3D12Resource* proxy_color()const{return proxy_.Get();}
    ID3D12Resource* nr_motion()const{return motion_.Get();}
    ID3D12Resource* nr_depth()const{return c_.useDepth?depth_.Get():nullptr;}
    ID3D12Resource* normalization_for_test()const{return norm_.Get();}
    const IntelInputConstants& constants()const{return c_;}
    bool Initialize(IntelInputShaders* shaders,ID3D12Resource* source,ID3D12Resource* motion,
        ID3D12Resource* exposure,ID3D12Resource* depth,DXGI_FORMAT depthViewFormat,ID3D12Resource* destination,ID3D12Resource* commit,
        const IntelInputConstants& constants) {
        if(!shaders||!source||!destination||!commit)return false;
        shaders_=shaders;c_=constants;source_=source;gameMotion_=motion;exposure_=exposure;
        destination_=destination;commit_=commit;gameDepth_=depth;format_=source->GetDesc().Format;
        auto* d=shaders->device.Get();
        for(auto* resource:{source,motion,exposure,depth,destination,commit})if(resource) {
            ComPtr<ID3D12Device> owner;
            if(FAILED(resource->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=d)return false;
        }
        if(source->GetDesc().Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)return false;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};support.Format=format_;
        if(!input_source_format(format_)||FAILED(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
           !(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)||!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))return false;
        proxy_=input_texture(d,c_.width,c_.height,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        motion_=input_texture(d,c_.width,c_.height,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(c_.useDepth)depth_=input_texture(d,c_.width,c_.height,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        norm_=input_texture(d,2,1,DXGI_FORMAT_R32G32B32A32_FLOAT);
        if(!proxy_||!motion_||(c_.useDepth&&!depth_)||!norm_)return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors=26;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap_))))return false;
        stride_=d->GetDescriptorHandleIncrementSize(hd.Type);
        for(UINT offset:{0u,13u}) {
            srv(offset,source,format_);
            srv(offset+1,motion,motion?motion->GetDesc().Format:DXGI_FORMAT_R16G16_FLOAT);
            srv(offset+2,nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT);
            srv(offset+3,exposure,exposure?exposure->GetDesc().Format:DXGI_FORMAT_R32_FLOAT);
            // Prepare doesn't read its proxy SRV; bind null to avoid an SRV/UAV alias.
            srv(offset+4,offset?proxy_.Get():nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT);
            srv(offset+5,depth,depthViewFormat);
            uav(offset+6,offset?nullptr:proxy_.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT);
            uav(offset+7,offset?nullptr:motion_.Get(),DXGI_FORMAT_R16G16_FLOAT);
            uav(offset+8,format_==DXGI_FORMAT_R11G11B10_FLOAT?destination:nullptr,format_);
            uav(offset+9,format_!=DXGI_FORMAT_R11G11B10_FLOAT?destination:nullptr,format_);
            uav(offset+10,norm_.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT);
            uav(offset+11,commit,DXGI_FORMAT_R32G32B32A32_FLOAT);
            uav(offset+12,offset?nullptr:depth_.Get(),DXGI_FORMAT_R32_FLOAT);
        }
        return true;
    }
    void RecordPrepare(ID3D12GraphicsCommandList* list,D3D12_RESOURCE_STATES sourceState,
        D3D12_RESOURCE_STATES motionState,D3D12_RESOURCE_STATES exposureState,D3D12_RESOURCE_STATES depthState) {
        input_barrier(list,source_.Get(),sourceState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,gameMotion_.Get(),motionState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,gameDepth_.Get(),depthState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,exposure_.Get(),exposureState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(c_.hdr&&c_.divisor==0) {
            input_uav_barrier(list,commit_.Get());input_uav_barrier(list,norm_.Get());
            Bind(list,shaders_->normalize.Get());list->Dispatch(1,1,1);input_uav_barrier(list,norm_.Get());
        }
        Bind(list,shaders_->prepare.Get());list->Dispatch((c_.width+7)/8,(c_.height+7)/8,1);
        input_barrier(list,proxy_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,motion_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,depth_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,source_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,sourceState);
        input_barrier(list,gameMotion_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,motionState);
        input_barrier(list,exposure_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,exposureState);
        input_barrier(list,gameDepth_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,depthState);
    }
    bool RecordComposite(ID3D12GraphicsCommandList* list,ID3D12Resource* neural,
        D3D12_RESOURCE_STATES sourceState,float shadowFloor,float knee,
        uint32_t style,float tone,float styleGain) {
        if(!neural||neural->GetDesc().Width!=c_.width||neural->GetDesc().Height!=c_.height||
           neural->GetDesc().Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)return false;
        srv(15,neural,DXGI_FORMAT_R32G32B32A32_FLOAT);
        c_.shadowFloor=shadowFloor;c_.hdrKnee=knee;c_.styleMode=style<=2?style:0;
        c_.styleTone=std::clamp(tone,0.0f,1.0f);c_.styleGain=std::clamp(styleGain,0.0f,2.0f);
        c_.styleSat=1+(style==1?-.10f:style==2?-.15f:0)*c_.styleTone;
        c_.styleExposure=std::pow(2.0f,-.10f*c_.styleTone);c_.styleContrast=-.25f*c_.styleTone;
        static const float lut[16]={0,.036957f,.072636f,.107120f,.140491f,.172830f,.204220f,.234741f,.264476f,.293507f,.321915f,.349782f,.377190f,.404220f,.430956f,.457477f};
        std::copy(std::begin(lut),std::end(lut),c_.curveLut);
        input_barrier(list,source_.Get(),sourceState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,destination_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Bind(list,format_==DXGI_FORMAT_R11G11B10_FLOAT?shaders_->composite.Get():shaders_->composite4.Get(),true);
        list->Dispatch((c_.width+7)/8,(c_.height+7)/8,1);
        input_barrier(list,destination_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input_barrier(list,source_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,sourceState);
        return true;
    }
};
} // namespace nrb
