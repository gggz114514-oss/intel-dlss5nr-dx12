// Adapted from the retained component-frame host: seven bounded frames,
// reset, mode return and intensity-zero full-resolution detail preservation.
// No backend implementation linked. GPU execution is opt-in and requires the shared GPU ticket.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include "nrb_vulkan_processor.h"
#include "nr_bridge.h"
using Microsoft::WRL::ComPtr;
static NRB_Processor registered{};
static bool output_borrowed=false;
static unsigned registrations=0,deregistrations=0;
static int capture(const void* p) {
    if(output_borrowed)return 0;
    if(!p){registered={};++deregistrations;return 1;}
    const auto& value=*static_cast<const NRB_Processor*>(p);
    if(value.abi_size!=sizeof(value)||!value.process||!value.retire)return 0;
    registered=value;++registrations;return 1;
}
static void check(HRESULT r){if(FAILED(r))throw std::runtime_error("HRESULT="+std::to_string(uint32_t(r)));}
static std::vector<char> bytes(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("input file "+p.string());
    std::vector<char> result(size_t(f.tellg()));f.seekg(0);f.read(result.data(),std::streamsize(result.size()));if(!f)throw std::runtime_error("input read");return result;
}
static std::string quoted(const std::string& s) {std::string q="\"";for(char c:s){if(c=='\\'||c=='\"')q+='\\';q+=c;}return q+'\"';}
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--help"){std::cout<<"DLL MANIFEST INPUT_ASSETS FRESH_OUT R_WIDTH R_HEIGHT O_WIDTH O_HEIGHT NR_HEIGHT(0=native) [baseline]\n";return 0;}
    if(argc!=10&&argc!=11)return 2;const bool switch_modes=argc==10;if(argc==11&&std::string(argv[10])!="baseline")return 2;const unsigned W=std::stoul(argv[5]),H=std::stoul(argv[6]),OW=std::stoul(argv[7]),OH=std::stoul(argv[8]),NRH=std::stoul(argv[9]);std::filesystem::path assets(argv[3]),out(argv[4]);if(!std::filesystem::create_directory(out))return 2;
    std::string stage="START";unsigned rendered=0,retired=0;int probe_rc=-999,attach_rc=-999,process_rc[7]={-999,-999,-999,-999,-999,-999,-999},retire_rc[7]={-999,-999,-999,-999,-999,-999,-999},stop_rc=-999;
    std::vector<unsigned> changed;std::vector<uint64_t> ready_values,consumer_values;
    auto mark=[&](const char* s){stage=s;std::ofstream(out/"checkpoint.txt")<<s<<"\n";std::cout<<s<<std::endl;};
    try {
        mark("LOAD_REAL_DLL_AND_PROBE");HMODULE module=LoadLibraryA(argv[1]);if(!module)throw std::runtime_error("DLL load");
        auto probe=reinterpret_cast<decltype(&NRVK_ComponentSelfTest)>(GetProcAddress(module,"NRVK_ComponentSelfTest"));
        auto attach=reinterpret_cast<decltype(&NRVK_AttachToBridge)>(GetProcAddress(module,"NRVK_AttachToBridge"));
        auto stop=reinterpret_cast<decltype(&NRVK_Shutdown)>(GetProcAddress(module,"NRVK_Shutdown"));
        if(!probe||!attach||!stop)throw std::runtime_error("required real component exports");
        NRVK_Config cfg{};cfg.abi_size=sizeof(cfg);cfg.manifest_dir=argv[2];cfg.output_ring_slots=1;
        std::string status=(out/"component_status.json").string();cfg.status_json_path=status.c_str();
        probe_rc=probe(&cfg,nullptr);if(probe_rc)throw std::runtime_error("actual probe rc="+std::to_string(probe_rc));
        mark("MATCHED_B580_D3D12_DEVICE");ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};check(a->GetDesc1(&d));if(d.VendorId==0x8086&&d.DeviceId==0xe20b){adapter=a;break;}}
        if(!adapter)throw std::runtime_error("B580 missing");ComPtr<ID3D12Device> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));check(list->Close());
        ComPtr<ID3D12Fence> completed;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completed)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");uint64_t serial=0;
        auto begin=[&](){check(allocator->Reset());check(list->Reset(allocator.Get(),nullptr));};
        auto finish=[&](){check(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);check(queue->Signal(completed.Get(),++serial));check(completed->SetEventOnCompletion(serial,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("D3D completion");};
        auto dep=[&](ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER d{};d.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;d.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,z};list->ResourceBarrier(1,&d);};
        auto texture=[&](DXGI_FORMAT format,UINT w,UINT h){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&r)));return r;};
        ComPtr<ID3D12Resource> inputs[]={texture(DXGI_FORMAT_R32G32B32A32_FLOAT,W,H),texture(DXGI_FORMAT_R32G32_FLOAT,W,H),texture(DXGI_FORMAT_R32_FLOAT,W,H)};
        auto sr_target=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,OW,OH);
        const char* names[]={"port_queue_backend_component_color.bin","port_queue_backend_component_motion.bin","port_queue_backend_component_depth.bin"};std::vector<char> input[3];D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[3]{};UINT rows[3];uint64_t row_bytes[3];uint64_t upload_bytes=0;
        for(unsigned i=0;i<3;++i){input[i]=bytes(assets/names[i]);auto d=inputs[i]->GetDesc();uint64_t size;device->GetCopyableFootprints(&d,0,1,(upload_bytes+511)&~511ULL,&fp[i],&rows[i],&row_bytes[i],&size);upload_bytes=fp[i].Offset+uint64_t(fp[i].Footprint.RowPitch)*rows[i];if(input[i].size()!=row_bytes[i]*rows[i])throw std::runtime_error("native input size");}
        auto host_buffer=[&](D3D12_HEAP_TYPE heap,uint64_t size,D3D12_RESOURCE_STATES state){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES hp{heap};ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;};
        auto upload=host_buffer(D3D12_HEAP_TYPE_UPLOAD,upload_bytes,D3D12_RESOURCE_STATE_GENERIC_READ);void* mapped;D3D12_RANGE no_read{0,0};check(upload->Map(0,&no_read,&mapped));for(unsigned i=0;i<3;++i)for(UINT y=0;y<rows[i];++y)memcpy(static_cast<char*>(mapped)+fp[i].Offset+uint64_t(y)*fp[i].Footprint.RowPitch,input[i].data()+uint64_t(y)*row_bytes[i],size_t(row_bytes[i]));upload->Unmap(0,nullptr);
        mark("INITIAL_CPU_UPLOAD_THEN_TRUE_PRODUCER_COMPLETION");begin();for(unsigned i=0;i<3;++i){dep(inputs[i].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp[i];dst.pResource=inputs[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);dep(inputs[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}finish();upload.Reset();
        mark("REAL_ATTACH_CAPTURE");attach_rc=attach(device.Get(),&cfg,&capture);if(attach_rc||registrations!=1||!registered.context||!registered.process||!registered.retire)throw std::runtime_error("actual Attach rc="+std::to_string(attach_rc));
        NRB_Frame frame{};frame.abi_size=sizeof(frame);frame.route=NRB_ROUTE_DLSS;frame.route_evidence=NRB_EVIDENCE_EXTERNAL_UNTAGGED;frame.device=device.Get();frame.queue=queue.Get();frame.command_list=list.Get();frame.color=inputs[0].Get();frame.motion=inputs[1].Get();frame.depth=inputs[2].Get();frame.output=sr_target.Get();frame.render_width=W;frame.render_height=H;frame.output_width=OW;frame.output_height=OH;frame.motion_scale_x=frame.motion_scale_y=1;frame.motion_scale_origin=NRB_MV_DLSS_NGX;frame.pre_exposure=frame.exposure_scale=1;frame.low_resolution_motion=1;frame.control_epoch=7;
        auto& n=frame.controls;n.abi_size=sizeof(n);n.enabled=1;n.input_height=NRH;n.style=0;n.history=NRB_HISTORY_FUSED;n.graph_replay=1;n.display_strength=n.model_intensity=n.local_tone=n.local_structure=1;n.skin_structure=-1;
        ComPtr<ID3D12Resource> readback;
        std::vector<float> reset_output;const uint64_t ids[]={0,1,0,2,3,0,1};
        for(unsigned i=0;i<7;++i){frame.frame_id=ids[i];frame.reset_history=i!=1&&i!=4&&i!=6;frame.controls.input_height=switch_modes&&(i==3||i==4)?(NRH==0?360:0):NRH;frame.controls.model_intensity=i==6?0.0f:1.0f;frame.control_epoch=i<3?7:i<5?8:9;NRB_Result result{};result.abi_size=sizeof(result);begin();mark(i==0?"ACTUAL_DLL_PROCESS_RESET":i==6?"ACTUAL_DLL_IDENTITY_RESIDUAL":"ACTUAL_DLL_PROCESS_HISTORY");process_rc[i]=registered.process(registered.context,&frame,&result);if(process_rc[i])throw std::runtime_error("actual processor rc="+std::to_string(process_rc[i]));if(!result.color||!result.ready_fence||!result.ready_value)throw std::runtime_error("actual output readiness missing");output_borrowed=true;
            auto d=result.color->GetDesc();if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT||d.Width!=W||d.Height!=H)throw std::runtime_error("NR source-sized RGBA32F output contract");D3D12_PLACED_SUBRESOURCE_FOOTPRINT dst_fp{};UINT h;uint64_t stride,size;device->GetCopyableFootprints(&d,0,1,0,&dst_fp,&h,&stride,&size);if(!readback)readback=host_buffer(D3D12_HEAP_TYPE_READBACK,size,D3D12_RESOURCE_STATE_COPY_DEST);
            check(queue->Wait(result.ready_fence,result.ready_value));dep(result.color,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=result.color;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=dst_fp;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);dep(result.color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);finish();
            if(result.ready_fence->GetCompletedValue()<result.ready_value)throw std::runtime_error("real ready fence not completed");ready_values.push_back(result.ready_value);consumer_values.push_back(serial);
            D3D12_RANGE range{0,size_t(size)};check(readback->Map(0,&range,&mapped));std::vector<float> pixels(size_t(W)*H*4);for(UINT y=0;y<h;++y)memcpy(pixels.data()+size_t(y)*W*4,static_cast<char*>(mapped)+dst_fp.Offset+uint64_t(y)*dst_fp.Footprint.RowPitch,size_t(stride));readback->Unmap(0,&no_read);
            unsigned difference=0;for(size_t k=0;k<pixels.size();++k){if(!std::isfinite(pixels[k]))throw std::runtime_error("non-finite output");if((k&3)==3){if(pixels[k]!=1.0f)throw std::runtime_error("application alpha");}else{float value;memcpy(&value,input[0].data()+k*4,4);if(pixels[k]!=value)++difference;}}
            if(i==6?difference!=0:difference==0)throw std::runtime_error(i==6?"zero residual changed original detail":"output unchanged from source with intensity1");if(i==0)reset_output=pixels;else if(i==2||i==5){if(pixels!=reset_output)throw std::runtime_error("reset/mode-return output differs from initial reset with identical seed");}changed.push_back(difference);std::ofstream dump(out/("frame"+std::to_string(i)+"_out_fp32.bin"),std::ios::binary);dump.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()*4));if(!dump)throw std::runtime_error("output dump");
            ++rendered;mark("RETIRE_TRUE_COMPLETED_CONSUMER");retire_rc[i]=registered.retire(registered.context,completed.Get(),serial);if(retire_rc[i])throw std::runtime_error("consumer retire rc="+std::to_string(retire_rc[i]));output_borrowed=false;++retired;result.color->Release();result.ready_fence->Release();
        }
        mark("HOST_STOP_AND_REAL_SHUTDOWN");stop_rc=stop();if(stop_rc||deregistrations!=1||registered.context)throw std::runtime_error("shutdown failed to unregister");CloseHandle(event);readback.Reset();sr_target.Reset();for(auto& x:inputs)x.Reset();completed.Reset();list.Reset();allocator.Reset();queue.Reset();device.Reset();adapter.Reset();factory.Reset();FreeLibrary(module);
        std::ofstream receipt(out/"receipt.json");receipt<<"{\"status\":\"MULTIRES_COMPONENT_SEVEN_FRAME_PASS\",\"normal_close\":true,\"frames_rendered\":"<<rendered<<",\"frames_retired\":"<<retired<<",\"render\":["<<W<<","<<H<<"],\"output\":["<<OW<<","<<OH<<"],\"nr_height\":"<<NRH<<",\"reset_history_mode_return_bit_exact\":true,\"zero_residual_original_detail_bit_exact\":true,\"finite_all\":true,\"alpha1\":true,\"performance_test\":false,\"game_or_stable_changed\":false}\n";
        mark("NORMAL_CLOSE");return 0;
    }catch(const std::exception& e){std::ofstream(out/"failure.json")<<"{\"status\":\"ACTUAL_COMPONENT_BRINGUP_FAIL\",\"stage\":"<<quoted(stage)<<",\"reason\":"<<quoted(e.what())<<",\"frames_rendered\":"<<rendered<<",\"frames_retired\":"<<retired<<",\"probe_rc\":"<<probe_rc<<",\"attach_rc\":"<<attach_rc<<",\"process_rc\":["<<process_rc[0]<<","<<process_rc[1]<<"],\"retire_rc\":["<<retire_rc[0]<<","<<retire_rc[1]<<"],\"performance_test\":false}\n";std::cerr<<stage<<": "<<e.what()<<std::endl;return 1;}
}
