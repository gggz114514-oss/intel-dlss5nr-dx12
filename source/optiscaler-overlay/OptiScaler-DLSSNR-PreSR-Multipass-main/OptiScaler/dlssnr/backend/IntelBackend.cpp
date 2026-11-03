#include "IntelBackend.h"
#include "LmxxfRecordingOwner.h"
#include "intel/nr_bridge.h"
#include "intel/nr_hdr_proxy.h"
#include "intel/nr_input_contract.h"
#include "intel/nr_intel_composite.h"
#include "../submission/CommandListProxy.h"
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <map>
#include <tuple>
#include <wrl/client.h>
namespace DlssNr::Backend {
using Microsoft::WRL::ComPtr;
namespace {
struct RuntimeConfig { uint32_t abi_size; const char* manifest_dir; const char* cache_dir; const char* status_json_path; uint32_t output_ring_slots, log_verbose; };
using AttachFn=int(*)(ID3D12Device*,const RuntimeConfig*,int(*)(const void*));
using ShutdownFn=int(*)();
// Registration contract (nrb_vulkan_processor.cpp nrvk_register / NRVK_Shutdown):
// the callback returns 1 to accept, and the exact same callback receives nullptr to
// deregister. It is invoked from the component's own thread, so the destination is a
// process-wide slot, not a thread-local. Returning 0 there makes Attach fail with -3
// and Shutdown report "borrowed" forever.
std::atomic<NRB_Processor*> g_registerSlot{nullptr};
// True while this thread is inside processor.process(). The runtime submits its own
// copy list on the game queue from there, which re-enters the expansion hook and the
// recording observer; retiring outputs at that point would lock the runtime's
// non-recursive context mutex twice and throw EDEADLK.
thread_local bool g_insideProcess=false;
int Register(const void* p) {
 auto* slot=g_registerSlot.load(std::memory_order_acquire);
 if(!slot)return 0;
 if(!p){*slot={};return 1;}                                  // deregistration
 const auto* v=static_cast<const NRB_Processor*>(p);
 if(v->abi_size!=sizeof(*v)||!v->process||!v->retire)return 0;
 *slot=*v;return 1;
}
void Transition(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
 if(!r||a==b)return; D3D12_RESOURCE_BARRIER x{}; x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);
}
// The runtime's status contract is one flat JSON object written per processed frame
// (nrb_vulkan_processor.cpp write_status). Read only the fields the panel needs.
std::string ReadTextFile(const std::filesystem::path& p) {
 std::ifstream in(p,std::ios::binary);
 if(!in)return {};
 return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
}
bool ScanU64(const std::string& s,const char* key,uint64_t& v) {
 const std::string needle=std::string("\"")+key+"\":";
 const auto at=s.find(needle); if(at==std::string::npos)return false;
 const char* p=s.c_str()+at+needle.size(); char* end=nullptr;
 const auto n=std::strtoull(p,&end,10); if(end==p)return false;
 v=n; return true;
}
bool ScanPair(const std::string& s,const char* key,uint32_t& a,uint32_t& b) {
 const std::string needle=std::string("\"")+key+"\":[";
 const auto at=s.find(needle); if(at==std::string::npos)return false;
 const char* p=s.c_str()+at+needle.size(); char* end=nullptr;
 const auto x=std::strtoul(p,&end,10); if(end==p||*end!=',')return false;
 p=end+1; const auto y=std::strtoul(p,&end,10); if(end==p)return false;
 a=uint32_t(x); b=uint32_t(y); return true;
}
}
struct IntelJob;
struct IntelBackend::Impl {
 ComPtr<ID3D12Device> device; std::filesystem::path directory; NRB_Processor processor{};
 // Full and Small ship their own component build and each manifest is only valid for
 // the build it was produced with. Keep one module per model; only one may be attached.
 struct Module { HMODULE dll=nullptr; AttachFn attach=nullptr; ShutdownFn shutdown=nullptr; };
 Module fullModule, smallModule;
 Module* active=&fullModule;
 Module& ModuleFor(int model) { return model?smallModule:fullModule; }
 static bool Open(Module& m,const std::filesystem::path& dllPath) {
  if(m.attach&&m.shutdown)return true;
  m.dll=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!m.dll)return false;
  m.attach=reinterpret_cast<AttachFn>(GetProcAddress(m.dll,"NRVK_AttachToBridge"));
  m.shutdown=reinterpret_cast<ShutdownFn>(GetProcAddress(m.dll,"NRVK_Shutdown"));
  return m.attach&&m.shutdown;
 }
 // The project's own control panel drives NR through nr_panel.ini (the same file
 // the standalone bridge component reads). It is watched here so the web panel
 // can switch network/resolution/style while the game runs.
 struct PanelState {
  bool present=false,enabled=true,autoMask=true,skinEnabled=false;
  int model=0; uint32_t inputHeight=0,style=0;
  float intensity=1,tone=0,structure=1,skin=0,displayStrength=1;
  uint64_t stamp=0;
 };
 PanelState panel; std::filesystem::path panelPath; uint64_t panelCheckedAt=0;
 // The title evaluates the feature several times per rendered frame with the same inputs
 // (measured 14.5 calls/frame in The Witcher 3). Repeating the whole submit for each of
 // them multiplies both the admission cost and the chance of hitting a slow runtime path.
 const void* sceneColour=nullptr; const void* sceneMotion=nullptr; float sceneJitterX=0,sceneJitterY=0;
 uint32_t sceneW=0,sceneH=0; ComPtr<ID3D12Resource> sceneOutput;
 nrb::HdrProxyCache shaders;
 nrb::IntelInputShaders inputShaders;
 using NormKey=std::tuple<uintptr_t,uint32_t,uint32_t,uint32_t>;
 struct NormStream {ComPtr<ID3D12Resource> gpu;uint64_t submittedAt=0,resetAt=0;};
 std::map<NormKey,std::shared_ptr<NormStream>> normalization;
 nrb::InputHistoryIdentity lastInput{};bool inputPrimed=false;
 uint32_t inputToken=0,lastSubmittedInputToken=0;
 std::deque<std::shared_ptr<IntelJob>> jobs;
 int model=-1; uint32_t lastHeight=0;
 // Guide contract actually used by the last recording, for the panel and the log.
 bool lastMotion=false;
 float lastMotionScaleX=0,lastMotionScaleY=0,lastMotionScaleXUsed=0,lastMotionScaleYUsed=0;
 std::string outcome,lastSource="ini",lastFailure,inputDescription;
 // Consecutive NRB_BYPASS_UNSYNCED results; a wedge inside the runtime context is
 // cleared by rebuilding it instead of bypassing for the rest of the session.
 int unsynced=0;
 // The runtime reports ready_fence only once its network retires, and in-game the
 // Small network sometimes never retires: the 2026-11-02 log shows 10 006/10 011/
 // 10 013/10 016/10 020/20 028 ms freezes, each ending exactly when the runtime's
 // own 10 s fence timeout fires and it reports NRB_BYPASS_UNSYNCED (code 3). A
 // recording that waits for that value therefore parks the whole title behind a
 // runtime hiccup. Nothing in a recording may wait for the runtime: frame N
 // composites the output frame N-1 already produced, checked on the CPU, so a
 // wedged network costs NR frames instead of the game.
 std::shared_ptr<IntelJob> pending; int lateStreak=0;
 // The output this host already has a queue wait parked on. Same-frame mode may only
 // queue the next wait once this one is delivered; otherwise a stalled runtime turns
 // one 10 s timeout into an unbounded freeze.
 ComPtr<ID3D12Fence> gateFence; UINT64 gateValue=0; uint64_t gateSetAt=0;
 // The queue the runtime submits its input copies on, plus the fence that orders it
 // behind the game's producer half. null falls back to the game queue.
 ComPtr<ID3D12CommandQueue> side; ComPtr<ID3D12Fence> sideFence; UINT64 sideValue=0;
 // A context that keeps wedging after a rebuild would freeze the game once per cycle.
 // A persistent sync failure suspends this session; model changes cannot reset it.
 bool latched=false; int latchedModel=0; uint64_t latchedAtFrame=0; uint64_t frame=0,completed=0,skippedReplay=0; bool reset=true,release=false;
 NRB_Controls lastControls{}; uint64_t controlEpoch=0;
 // One line per abnormal frame: written only when the interval between two NR calls
 // exceeds a threshold, because the engine log rotates every few seconds at this
 // verbosity and a spike has to leave its own record behind.
 uint64_t lastRecordAt=0;
 double emaInterval=0;
 void LogLongFrame(uint64_t now) {
  // Frame generation is steady, so a hitch is a deviation from the running interval,
  // not a large absolute value. Log both directions against an EMA.
  const double interval=lastRecordAt?double(now-lastRecordAt):0.0;
  if(lastRecordAt&&emaInterval>1.0){
   const bool spike=interval>emaInterval*1.5+2.0;
   const bool drop=interval<emaInterval*0.55;
   if(hostSubmitLast>200.0){
    std::ofstream log(directory/L"nr-intel"/L"nr_frame.log",std::ios::app);
    if(log) log<<now<<" slow gap_ms="<<int(interval)
     <<" ema_ms="<<int(emaInterval)<<" submit_ms="<<int(hostSubmitLast)
     <<" retained="<<jobs.size()<<" frame="<<frame
     <<" unsynced="<<unsynced<<" late="<<lateStreak<<"\\n";
   }
  }
  if(interval>0.0) emaInterval=emaInterval>0.0?emaInterval*0.9+interval*0.1:interval;
  lastRecordAt=now;
 }
 std::string status="intel: ready to initialize";
 std::filesystem::path statusPath; uint64_t statusReadAt=0;
 std::string manifestDir,statusPathText;
 // Host submit cost (includes the runtime's previous-network wait). Reported so the
 // panel separates frame pacing from GPU work instead of implying one number.
 double hostSubmitMs=0,hostSubmitLast=0; uint64_t hostSubmitSamples=0;
 void NoteHostSubmitMs(double ms) {
  // The first processed frame carries Vulkan/runtime initialization; it is not a
  // frame cost and must not dominate the reported number.
  hostSubmitLast=ms;
  if(hostSubmitSamples++==0)return;
  hostSubmitMs=hostSubmitSamples==2?ms:hostSubmitMs*0.9+ms*0.1;
 }
 // Last runtime-reported identity/extents. The panel shows facts, not settings.
 // ring_full/gpu_timeout/slots_in_flight are the runtime's own counters: they are what
 // distinguishes "the network stopped delivering" from "the host did not retire".
 struct Actual { bool valid=false; uint32_t renderW=0,renderH=0,networkW=0,networkH=0,outputW=0,outputH=0; uint64_t processed=0,bypass=0,ringFull=0,gpuTimeout=0,slotsInFlight=0; uint32_t lastStatus=0,initState=0; };
 Actual actual;
 // Polled, not per frame: the panel rewrites the file when the user moves a
 // control, so a 250 ms stamp check is enough and costs one directory query.
 void RefreshPanel() {
  const uint64_t now=GetTickCount64();
  if(now-panelCheckedAt<250)return;
  panelCheckedAt=now;
  WIN32_FILE_ATTRIBUTE_DATA attr{};
  if(!GetFileAttributesExW(panelPath.c_str(),GetFileExInfoStandard,&attr)){panel.present=false;return;}
  const uint64_t stamp=(uint64_t(attr.ftLastWriteTime.dwHighDateTime)<<32)|attr.ftLastWriteTime.dwLowDateTime;
  if(panel.present&&stamp==panel.stamp)return;
  const auto text=ReadTextFile(panelPath);
  if(text.empty()){panel.present=false;return;}
  PanelState next{};next.present=true;next.stamp=stamp;
  bool inNr=false;size_t pos=0;
  while(pos<text.size()){
   auto end=text.find('\n',pos);if(end==std::string::npos)end=text.size();
   std::string line=text.substr(pos,end-pos);pos=end+1;
   while(!line.empty()&&(line.back()=='\r'||line.back()==' '||line.back()=='\t'))line.pop_back();
   const auto first=line.find_first_not_of(" \t");
   if(first==std::string::npos)continue;
   line=line.substr(first);
   if(line.front()=='['){inNr=_stricmp(line.c_str(),"[NR]")==0;continue;}
   if(!inNr)continue;
   const auto eq=line.find('=');if(eq==std::string::npos)continue;
   std::string key=line.substr(0,eq);
   while(!key.empty()&&(key.back()==' '||key.back()=='\t'))key.pop_back();
   std::string value=line.substr(eq+1);
   const auto v0=value.find_first_not_of(" \t");
   value=v0==std::string::npos?std::string():value.substr(v0);
   const auto numeric=[&]{return float(std::atof(value.c_str()));};
   if(_stricmp(key.c_str(),"enabled")==0)next.enabled=std::atoi(value.c_str())!=0;
   else if(_stricmp(key.c_str(),"backend")==0){
    // "102"/"full" is the Full network, "small" the Small one; a manifest path is
    // matched by the same words so a copied panel config keeps working.
    std::string lowered=value;for(auto& c:lowered)c=char(tolower((unsigned char)c));
    next.model=lowered.find("small")!=std::string::npos?1:0;
   }
   else if(_stricmp(key.c_str(),"input_height")==0)next.inputHeight=uint32_t(std::strtoul(value.c_str(),nullptr,10));
   else if(_stricmp(key.c_str(),"style")==0)next.style=uint32_t(std::strtoul(value.c_str(),nullptr,10));
   else if(_stricmp(key.c_str(),"intensity")==0)next.intensity=numeric();
   else if(_stricmp(key.c_str(),"local_tone")==0)next.tone=numeric();
   else if(_stricmp(key.c_str(),"local_structure")==0)next.structure=numeric();
   else if(_stricmp(key.c_str(),"skin_structure")==0)next.skin=numeric();
   else if(_stricmp(key.c_str(),"display_strength")==0)next.displayStrength=numeric();
   else if(_stricmp(key.c_str(),"auto_mask")==0)next.autoMask=std::atoi(value.c_str())!=0;
  }
  panel=next;
 }
 Impl(ID3D12Device* d,const std::filesystem::path& path):device(d),directory(path) {
  g_registerSlot.store(&processor,std::memory_order_release);
  shaders.set_enabled(true);
  statusPath=path/L"nr-intel"/L"status.json";
  panelPath=path/L"nr-intel"/L"nr_panel.ini";
  D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
  if(SUCCEEDED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&side)))&&
     FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&sideFence))))side.Reset();
 }
 // Requested height and model are settings; what actually ran comes from the runtime.
 std::string Describe(const char* state) const {
  char b[256]{};
  const int requested=int(lastHeight);
  if(actual.valid)
   _snprintf_s(b,sizeof(b),_TRUNCATE,"%s; %ux%u -> net %ux%u -> out %ux%u; frames %llu, bypass %llu, status %u; ring %llu, timeout %llu, inflight %llu",
    state,actual.renderW,actual.renderH,actual.networkW,actual.networkH,actual.outputW,actual.outputH,
    (unsigned long long)actual.processed,(unsigned long long)actual.bypass,actual.lastStatus,
    (unsigned long long)actual.ringFull,(unsigned long long)actual.gpuTimeout,
    (unsigned long long)actual.slotsInFlight);
  else
   _snprintf_s(b,sizeof(b),_TRUNCATE,"%s; frames %llu, replays %llu",
    state,(unsigned long long)completed,(unsigned long long)skippedReplay);
  std::string s=b;
  s+="; request ";
  s+=requested?(std::to_string(requested)+"p"):std::string("native");
  s+=model==1?" / small":" / full";
  s+="; source ";s+=lastSource=="panel"?"panel":"OptiScaler.ini";
  {
   char t[128]{};
   _snprintf_s(t,sizeof(t),_TRUNCATE,"; mv %s (raw %.5f/%.5f, used %.3f/%.3f)",
    lastMotion?"game":"zero",lastMotionScaleX,lastMotionScaleY,lastMotionScaleXUsed,lastMotionScaleYUsed);
   s+=t;
  }
  if(hostSubmitSamples)
  {
   char t[64]{};
   _snprintf_s(t,sizeof(t),_TRUNCATE,"; submit %.0f ms avg",hostSubmitMs);
   s+=t;
  }
  s+="; input ";s+=inputDescription;
  return s;
 }
 void RefreshActual() {
  if(statusPath.empty())return;
  const uint64_t now=GetTickCount64();
  if(now-statusReadAt<250)return;
  statusReadAt=now;
  const auto text=ReadTextFile(statusPath);
  if(text.empty())return;
  Actual a{}; uint64_t v=0;
  if(ScanU64(text,"processed_count",v))a.processed=v;
  if(ScanU64(text,"bypass",v))a.bypass=v;
  if(ScanU64(text,"ring_full",v))a.ringFull=v;
  if(ScanU64(text,"gpu_timeout",v))a.gpuTimeout=v;
  if(ScanU64(text,"slots_in_flight",v))a.slotsInFlight=v;
  if(ScanU64(text,"last_status",v))a.lastStatus=uint32_t(v);
  if(ScanU64(text,"init_state",v))a.initState=uint32_t(v);
  const bool geometry=ScanPair(text,"render",a.renderW,a.renderH)&&
   ScanPair(text,"network",a.networkW,a.networkH)&&
   ScanPair(text,"output",a.outputW,a.outputH);
  a.valid=geometry&&a.renderW&&a.networkW;
  actual=a;
 }
 bool Ensure(int requested) {
  if(processor.process&&model==requested){release=false;return true;}
  if(processor.process){if(active->shutdown()!=0){status="intel: finishing previous model";return false;}processor={};}
  const auto package=directory/L"nr-intel"/(requested?L"small":L"full");
  auto& module=ModuleFor(requested);
  if(!module.attach&&!Open(module,package/L"nrvk_bridge.dll")&&!Open(module,directory/L"nrvk_bridge.dll")){
   status="intel: nrvk_bridge.dll missing or incompatible";return false;
  }
  if(!module.attach||!module.shutdown){status="intel: incompatible Vulkan runtime";return false;}
  active=&module;
  // The component shallow-copies the config and dereferences these pointers on the
  // first processed frame, so the strings must live as long as the attachment.
  manifestDir=(package/L"manifest").string();
  statusPathText=statusPath.string();
  // Component-side stderr tracing is opt-in: a shipped game has no console.
  const bool verbose=GetEnvironmentVariableW(L"OPTISCALER_INTEL_NR_VERBOSE",nullptr,0)>0||
  GetFileAttributesW((directory/L"nr-intel"/L"nr_trace.on").c_str())!=INVALID_FILE_ATTRIBUTES;
  RuntimeConfig cfg{sizeof(RuntimeConfig),manifestDir.c_str(),nullptr,statusPathText.c_str(),3,verbose?1u:0u};
  // The slot stays valid for the Impl lifetime: NRVK_Shutdown deregisters through it.
  g_registerSlot.store(&processor,std::memory_order_release);
  int rc=module.attach(device.Get(),&cfg,Register);
  if(rc==0&&!processor.process){
   // The component still had a context from a previous session, so register()
   // returned OK without ever calling the host callback. Tear that context down
   // and attach again; otherwise every frame would fail with an empty processor.
   if(module.shutdown()==0)rc=module.attach(device.Get(),&cfg,Register);
  }
  if(rc||!processor.process){status="intel: component init failed ("+std::to_string(rc)+")";return false;}
  model=requested;reset=true;release=false;actual={};status=Describe(requested?"small ready":"full ready");
  return true;
 }
 void Collect();
};
struct IntelJob final : Submission::RecordingObserver, std::enable_shared_from_this<IntelJob> {
 std::shared_ptr<IntelBackend::Impl> owner;
 ComPtr<ID3D12Resource> source,motion,output,nrOutput;
 ComPtr<ID3D12Fence> ready,tail; UINT64 tailValue=0;
 // Value the runtime must reach before this job's nrOutput may be read. The next
 // recording consumes it; nothing waits for it inside this one.
 UINT64 readyValue=0;
 ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> composite;
 std::unique_ptr<nrb::IntelInputAdapter> hdr; NRB_Frame frame{}; int model=0;
 nrb::InputHistoryIdentity inputIdentity{};uint32_t inputToken=0;bool inputReplay=false,gameReset=false;
 std::shared_ptr<IntelBackend::Impl::NormStream> normStream;
 // Output-composition settings captured at record time; Between runs on the job.
 float shadowHold=0.35f, knee=1.2f;
 D3D12_RESOURCE_STATES sourceState{};
 bool invalidated=false,submitted=false,retired=false,executed=false,skip=false;
 // Wait for this frame's own network instead of the previous frame's output.
 bool sameFrame=false;
 explicit IntelJob(std::shared_ptr<IntelBackend::Impl> p):owner(std::move(p)){}
 // Our composite was submitted on this queue and is covered by tail/tailValue.
 bool Done() const {
  if(!executed)return true;
  if(!tail)return false;                       // no fence recorded for that execution yet
  const auto v=tail->GetCompletedValue();
  return v!=UINT64_MAX&&v>=tailValue;
 }
 void ReleaseComposite() {
  allocator.Reset();composite.Reset();nrOutput.Reset();ready.Reset();
  retired=false;submitted=false;
 }
 HRESULT BeforeExecute(const Submission::RecordingExecution&) noexcept override {
  try {
   owner->Collect();                          // retire outputs whose consumer fence completed
   // A closed recording can be executed again while the previous execution is still
   // on the GPU. The framework turns a failed BeforeExecute into a dropped game
   // list, so never fail here: the producer already replays the original-colour
   // copy, and this execution carries no NR.
   inputReplay=executed;
   skip=executed&&!Done();
   if(skip)++owner->skippedReplay;
   return S_OK;
  } catch(...) { skip=true; return S_OK; }
 }
 void Between(const Submission::RecordingExecution& e) noexcept override {
  try {
   owner->Collect();
   // Sync contract: the framework calls Between only after the producer half was
   // submitted, under Submission::RecordingMutex. process() may CPU-wait for this
   // context's previous network fence, but every submission that can signal it was
   // made by this same serialized path, so the wait cannot need this lock. Calling
   // process() before the producer submission would wait across work that has not
   // been queued yet; that is refused here instead of stalling the game thread.
   if(skip||invalidated||owner->latched||!e.producerSubmitted||!e.queue){return;}
   if(!inputReplay&&normStream) {
    normStream->submittedAt=GetTickCount64();
    if(gameReset)normStream->resetAt=normStream->submittedAt;
   }
   if(executed)ReleaseComposite();           // Done() was verified in BeforeExecute
   executed=true;
   auto* d=owner->device.Get();
   if(FAILED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))) ||
      FAILED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&composite)))){
    owner->status="intel: composite allocation failed";return;
   }
   // A different network means a different component build and a new context: the
   // output the previous one still owes this host belongs to textures that shutdown
   // frees, so it is dropped before Ensure() switches.
   if(model!=owner->model){owner->pending.reset();owner->gateFence.Reset();owner->gateValue=0;}
   if(!owner->Ensure(model))return;
   // The runtime submits its input copies on the queue it is handed and signals the
   // copy timeline the game queue would otherwise have to produce. Handing it the game
   // queue made its Vulkan submit depend on a queue our own composite wait can park;
   // that dependency is what the 2026-11-02 capture shows as gpu_timeout=1 followed by
   // a 28 s freeze. Give it its own queue instead, ordered behind the producer half by
   // a fence the game queue signals after that submission.
   const bool diagnoseGameQueue=std::getenv("NR_INTEL_DIAG_GAME_QUEUE")!=nullptr;
   if(!diagnoseGameQueue&&owner->side&&owner->sideFence){
    if(FAILED(e.queue->Signal(owner->sideFence.Get(),++owner->sideValue)) ||
       FAILED(owner->side->Wait(owner->sideFence.Get(),owner->sideValue))){
     owner->status="intel: side queue ordering failed; original input";owner->reset=true;return;
    }
   }
   // Never stack a second wait for the runtime behind one that is overdue. One frame of
   // latency is normal (the runtime keeps one network in flight), so the gate only
   // trips once the outstanding output is more than 100 ms late: that is the state the
   // 2026-11-02 capture shows as gpu_timeout=1 followed by a 28 s freeze, where every
   // frame added another wait for a fence that was still outstanding.
   if(sameFrame&&owner->gateFence&&owner->gateFence->GetCompletedValue()<owner->gateValue&&
      GetTickCount64()-owner->gateSetAt>100){
    {
     std::ofstream log(owner->directory/L"nr-intel"/L"nr_stall.log",std::ios::app);
     if(log) log<<GetTickCount64()<<" gate age_ms="<<(GetTickCount64()-owner->gateSetAt)
      <<" frame="<<owner->frame<<"\n";
    }
    ++owner->lateStreak;
    owner->lastFailure="output not delivered x"+std::to_string(owner->lateStreak);
    owner->status="intel: output not delivered; original input";
    owner->reset=true;
    return;
   }
   frame.queue=(!diagnoseGameQueue&&owner->side)?owner->side.Get():e.queue;frame.frame_id=++owner->frame;
   if(std::memcmp(&owner->lastControls,&frame.controls,sizeof(frame.controls))!=0){
    owner->lastControls=frame.controls;++owner->controlEpoch;owner->reset=true;
   }
   frame.control_epoch=owner->controlEpoch;
   frame.reset_history=nrb::input_history_reset(owner->lastInput,inputIdentity,owner->inputPrimed,
       frame.reset_history||owner->reset,inputReplay||inputToken<=owner->lastSubmittedInputToken)?1u:0u;
   NRB_Result r{};r.abi_size=sizeof(r);
   // Wall time of the host submit. It includes the runtime's wait for this
   // context's previous network fence, so it is the frame's NR admission cost on
   // the game thread, not GPU time.
   LARGE_INTEGER t0{},t1{},freq{};
   QueryPerformanceFrequency(&freq);
   QueryPerformanceCounter(&t0);
   g_insideProcess=true;
   const int rc=owner->processor.process(owner->processor.context,&frame,&r);
   g_insideProcess=false;
   QueryPerformanceCounter(&t1);
   if(freq.QuadPart>0)
    owner->NoteHostSubmitMs(double(t1.QuadPart-t0.QuadPart)*1000.0/double(freq.QuadPart));
   if(rc!=NRB_OK||!r.color||!r.ready_fence){
    // Which UNSYNCED site inside the runtime fired, and what it cost, is the only way
    // to tell "the runtime waited out its own 10 s fence" from "it returned at once and
    // our queued composite wait is what stalls the title". One line per failure.
    {
     std::ofstream log(owner->directory/L"nr-intel"/L"nr_stall.log",std::ios::app);
     if(log) log<<GetTickCount64()<<" rc="<<rc<<" submit_ms="
      <<(freq.QuadPart?int((t1.QuadPart-t0.QuadPart)*1000/freq.QuadPart):-1)
      <<" unsynced="<<owner->unsynced<<" frame="<<owner->frame
      <<" in_flight="<<owner->jobs.size()<<"\n";
    }
    // UNSYNCED covers both "previous network still running" and "the copy fence never
    // completed". The second case never clears inside one context: every later frame
    // sees the same stale fence. Record suspends admission without freeing resources
    // that may still belong to unfinished GPU work.
    if(rc==NRB_BYPASS_UNSYNCED){
     if(owner->unsynced==0){
      LOG_ERROR("Intel NR retirement wait history: count={} timeouts={} last_ms={} last_result={}",
       Submission::g_retireWaitCount.load(),Submission::g_retireWaitTimeouts.load(),
       Submission::g_retireWaitLastMs.load(),Submission::g_retireWaitLastResult.load());
      LOG_ERROR("Intel NR first UNSYNCED: model={} frame={} gate={}/{} side={}/{} device=0x{:08X}",
       owner->model,owner->frame,
       owner->gateFence?owner->gateFence->GetCompletedValue():0,owner->gateValue,
       owner->sideFence?owner->sideFence->GetCompletedValue():0,owner->sideValue,
       unsigned(owner->device->GetDeviceRemovedReason()));
      for(const auto& job:owner->jobs){
       LOG_ERROR("Intel NR retained job: frame={} executed={} submitted={} retired={} ready={}/{} tail={}/{}",
        job->frame.frame_id,job->executed,job->submitted,job->retired,
        job->ready?job->ready->GetCompletedValue():0,job->readyValue,
        job->tail?job->tail->GetCompletedValue():0,job->tailValue);
      }
     }
     ++owner->unsynced;
     owner->lastFailure="runtime unsynced (code 3) x"+std::to_string(owner->unsynced);
    } else owner->unsynced=0;
    owner->status="intel: original input (runtime rc "+std::to_string(rc)+")";owner->reset=true;return;
   }
   owner->lastInput=inputIdentity;owner->inputPrimed=true;
   owner->lastSubmittedInputToken=inputToken;
   owner->unsynced=0;owner->lastFailure.clear();
   nrOutput.Attach(r.color);ready.Attach(r.ready_fence);readyValue=r.ready_value;
   // Default: publish this frame's output for the next recording and consume the one
   // already produced, so no recording ever waits for the runtime and its fence cannot
   // park the game queue. Same-frame mode trades that for zero NR age and the wait the
   // 2026-11-02 log shows stalling for 10 s when the Small network stops delivering.
   ID3D12Resource* nrSource=nullptr;
   if(sameFrame){
    if(SUCCEEDED(e.queue->Wait(ready.Get(),r.ready_value))){nrSource=nrOutput.Get();
      owner->gateFence=ready;owner->gateValue=r.ready_value;owner->gateSetAt=GetTickCount64();}
   } else {
    auto produced=owner->pending;
    owner->pending=shared_from_this();
    if(produced&&produced->ready&&produced->nrOutput&&
       produced->ready->GetCompletedValue()>=produced->readyValue)nrSource=produced->nrOutput.Get();
   }
   // Our own write-back: the shared proxy would replace every pixel below its HDR
   // knee with the network result, which turns any network frame-to-frame variation
   // into visible shadow crawl. The host keeps a title-owned share in the dark end.
   
   if(nrSource&&hdr->RecordComposite(composite.Get(),nrSource,sourceState,
       shadowHold,knee,frame.controls.style,frame.controls.local_tone,frame.controls.display_strength)){
    owner->lateStreak=0;
    owner->reset=false;
   } else {
    // Nothing the title can consume this frame: the fallback colour copy recorded by
    // Record() stands, and the runtime keeps its slot until Collect() retires it.
    ++owner->lateStreak;
    owner->lastFailure=sameFrame?"same-frame wait failed":"output not delivered x"
     +std::to_string(owner->lateStreak);
    owner->status=sameFrame?"intel: same-frame wait failed; original input":"intel: output not delivered; original input";
    owner->reset=true;
   }
   // The runtime owns this output until retire(), so it counts as in flight either way.
   submitted=true;
   owner->outcome=owner->Describe(owner->model?"small network running":"full network running");
   if(nrSource)owner->status=owner->outcome;
   // A later replay of this closed recording re-executes the producer's prepare,
   // which assumes both proxy surfaces start in UNORDERED_ACCESS.
   Transition(composite.Get(),hdr->proxy_color(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   Transition(composite.Get(),hdr->nr_motion(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   Transition(composite.Get(),hdr->nr_depth(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   if(SUCCEEDED(composite->Close())){ID3D12CommandList* list=composite.Get();e.queue->ExecuteCommandLists(1,&list);}
   else {submitted=false;owner->status="intel: composite close failed; original input";owner->reset=true;}
  } catch(const std::exception& e) {owner->status=std::string("intel: submit error (")+e.what()+")";owner->reset=true;}
   catch(...) {owner->status="intel: output not delivered; original input";owner->reset=true;}
 }
 void Executed(const Submission::RecordingExecution& e) noexcept override {
  if(!e.fence)return;
  tail=e.fence;tailValue=e.fenceValue;
 }
 void Invalidated(Submission::RecordingIdentity) noexcept override {invalidated=true;}
};

namespace {
using IntelToken=std::shared_ptr<IntelJob>;
int32_t IntelDestroy(void*) { return LMXXF_NR_OK; }
int32_t IntelReset(void*) { return LMXXF_NR_OK; }
int32_t IntelError(char* text,uint32_t size) { if(text&&size)text[0]=0;return LMXXF_NR_OK; }
int32_t IntelBegin(void*,void* token,void* queue) {
 auto& j=*static_cast<IntelToken*>(token);
 Submission::RecordingExecution e{};e.queue=static_cast<ID3D12CommandQueue*>(queue);
 return SUCCEEDED(j->BeforeExecute(e))?LMXXF_NR_OK:LMXXF_NR_FAILED;
}
int32_t IntelEnqueue(void*,void* token,void* queue) {
 auto& j=*static_cast<IntelToken*>(token);
 Submission::RecordingExecution e{};e.queue=static_cast<ID3D12CommandQueue*>(queue);e.producerSubmitted=true;
 j->Between(e);
 return j->skip||j->submitted?LMXXF_NR_OK:LMXXF_NR_FAILED;
}
int32_t IntelEnd(void*,void* token,void* queue,uint32_t flags,void* fence,uint64_t value,int32_t status) {
 auto& j=*static_cast<IntelToken*>(token);
 // A bypassed overlapping replay must not replace the original execution's certificate.
 if(j->skip)return LMXXF_NR_OK;
 Submission::RecordingExecution e{};e.queue=static_cast<ID3D12CommandQueue*>(queue);
 e.producerSubmitted=(flags&LMXXF_NR_SUBMITTED_PRODUCER)!=0;
 e.continuationSubmitted=(flags&LMXXF_NR_SUBMITTED_CONSUMER)!=0;e.status=status;
 if(SUCCEEDED(status)&&e.producerSubmitted&&e.continuationSubmitted){
  e.fence=static_cast<ID3D12Fence*>(fence);e.fenceValue=value;j->Executed(e);
 }
 return LMXXF_NR_OK;
}
int32_t IntelInvalidate(void*,void* token){(*static_cast<IntelToken*>(token))->invalidated=true;return LMXXF_NR_OK;}
int32_t IntelCollect(void*,void* token){
 auto* t=static_cast<IntelToken*>(token);auto& j=*t;
 j->owner->Collect();
 if(!j->invalidated||!j->Done()||(j->submitted&&!j->retired))return LMXXF_NR_UNAVAILABLE;
 delete t;return LMXXF_NR_OK;
}
std::shared_ptr<LmxxfRecording::Lease> AttachIntelRecording(const std::shared_ptr<IntelJob>& j,Submission::ILogicalCommandList* list){
 LmxxfNrApi api{};api.struct_size=sizeof(api);api.abi_version=LMXXF_NR_ABI_VERSION;
 api.Destroy=IntelDestroy;api.ResetHistory=IntelReset;api.GetLastError=IntelError;
 api.BeginRecordingExecution=IntelBegin;api.EnqueueHip=IntelEnqueue;api.EndRecordingExecution=IntelEnd;
 api.InvalidateRecording=IntelInvalidate;api.CollectRecording=IntelCollect;
 auto owner=LmxxfRecording::SessionOwner::Create(api,nullptr);
 if(!owner)return {};
 return LmxxfRecording::Attach(owner,new IntelToken(j),list);
}
}
void IntelBackend::Impl::Collect() {
 // process() submits the runtime's own copy list on the game queue. That submission
 // re-enters the expansion hook, which reaches the recording observer again while the
 // runtime still holds its non-recursive context mutex. Retiring from there throws
 // "resource deadlock would occur" and drops NR for that frame, so retirement waits
 // for the next frame outside the runtime call.
 if(g_insideProcess)return;
 // The pending output is still owned by this host until the next recording consumes
 // it; retiring it early would hand its texture back to the runtime while a composite
 // that reads it is already recorded.
 for(auto& j:jobs) if(j->submitted&&!j->retired&&j!=pending&&j->Done()) {
  if(processor.retire && processor.retire(processor.context,j->tail.Get(),j->tailValue)==NRB_OK){j->retired=true;++completed;}
 }
 std::erase_if(jobs,[this](const auto& j){return j!=pending&&j->invalidated&&j->Done()&&(!j->submitted||j->retired);});
}
IntelBackend::IntelBackend(ID3D12Device* d,ID3D12CommandQueue*,const std::filesystem::path& dir):p(std::make_shared<Impl>(d,dir)){}
IntelBackend::~IntelBackend(){ReleaseSession();}
ID3D12Resource* IntelBackend::Record(ID3D12GraphicsCommandList* cmd,const AmdPreSr::Frame& f,const AmdPreSr::Settings& settings) {
 std::lock_guard lock(Submission::RecordingMutex());p->LogLongFrame(GetTickCount64());p->Collect();
 // Failed submissions can still own GPU resources. Never destroy the runtime or
 // discard its fences under RecordingMutex to recover from a stalled queue.
 if(!p->latched&&(p->unsynced>=90||p->lateStreak>=120)&&p->processor.process){
  p->latched=true;
  p->latchedModel=p->model;
  p->latchedAtFrame=p->frame;
  p->lastFailure=p->lateStreak>=120?"suspended after 120 late frames":"suspended after 90 unsynced frames";
  LOG_ERROR("Intel NR suspended: model={} frame={} unsynced={} late={} jobs={} gate={}/{} side={}/{} device=0x{:08X}",
   p->model,p->frame,p->unsynced,p->lateStreak,p->jobs.size(),
   p->gateFence?p->gateFence->GetCompletedValue():0,p->gateValue,
   p->sideFence?p->sideFence->GetCompletedValue():0,p->sideValue,
   unsigned(p->device->GetDeviceRemovedReason()));
 }
 if(p->latched){
  // A model toggle must not re-enter Ensure() and shut down this failed context.
  // Normal session retirement retains ownership until the recorded work is done.
  p->status="intel: NR suspended after sync failures; restart game to retry; original input";
  return nullptr;
 }
 if(!cmd||!f.colour||!f.width||!f.height)return nullptr;
 if(!f.gameUpscalerInput){
  // Same-frame pre-SR only: the composite is ordered inside this recording, so a
  // post-SR frame would carry the SR result as NR input.
  p->status="intel: post-SR placement unsupported; original input";return nullptr;
 }
 ComPtr<Submission::ILogicalCommandList> logical;ComPtr<Submission::IRecordingResources> resources;
 if(FAILED(cmd->QueryInterface(IID_PPV_ARGS(&logical)))){
  p->status="intel: game list is not a wrapped recording; original input";return nullptr;
 }
 if(FAILED(cmd->QueryInterface(IID_PPV_ARGS(&resources)))||logical->IsSplitIneligible()){
  p->status=std::string("intel: no eligible submission split (")+logical->SplitRejectionReason()+"); original input";
  static std::string lastSplitRejection; if(lastSplitRejection!=p->status){lastSplitRejection=p->status;LOG_WARN("{}",p->status);}
  return nullptr;
 }
 const auto desc=f.colour->GetDesc();
 const nrb::InputRect colorRect{f.colourX,f.colourY,f.width,f.height};
 const auto colorContract=nrb::input_color_contract(nrb::input_storage(desc.Format),f.createFlagsKnown,f.hdrInput,
     static_cast<nrb::InputEncoding>(settings.intel.sourceEncoding),settings.encoding,
     static_cast<nrb::InputCodec>(settings.intel.codec),settings.intel.linearUnitNits);
 if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||!colorRect.fits(UINT(desc.Width),desc.Height)||desc.SampleDesc.Count!=1||desc.MipLevels!=1||desc.DepthOrArraySize!=1||
    !nrb::input_source_format(desc.Format)||!colorContract.valid||settings.intel.sourceEncoding>5||settings.intel.codec>2){
  p->status="intel: unsupported input format/rect or ambiguous encoding; set Input source encoding; original input";
  return nullptr;
 }
 const auto md=f.motion?f.motion->GetDesc():D3D12_RESOURCE_DESC{};
 const auto dd=f.depth?f.depth->GetDesc():D3D12_RESOURCE_DESC{};
 const nrb::InputRect motionRect{f.motionX,f.motionY,f.motionWidth?f.motionWidth:f.width,f.motionHeight?f.motionHeight:f.height};
 const nrb::InputRect depthRect{f.depthX,f.depthY,f.depthWidth?f.depthWidth:f.width,f.depthHeight?f.depthHeight:f.height};
 // The proxy ABI has no jitter-removal contract; depth is resampled separately.
 // Unknown/jittered guides explicitly request a history reset; zero motion is never
 // presented as a valid static guide. Colour processing still works on this frame.
 const bool useGameMotion=f.createFlagsKnown&&f.motion&&motionRect.fits(UINT(md.Width),md.Height)&&
     md.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&md.SampleDesc.Count==1&&md.MipLevels==1&&md.DepthOrArraySize==1&&
     nrb::input_motion_format(md.Format)&&!f.motionJittered&&std::isfinite(f.motionScaleX)&&std::isfinite(f.motionScaleY);
 DXGI_FORMAT depthView=dd.Format==DXGI_FORMAT_R32_TYPELESS?DXGI_FORMAT_R32_FLOAT:
     dd.Format==DXGI_FORMAT_R24G8_TYPELESS?DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
     dd.Format==DXGI_FORMAT_R16_TYPELESS?DXGI_FORMAT_R16_UNORM:dd.Format;
 const bool useDepth=f.depth&&depthRect.fits(UINT(dd.Width),dd.Height)&&dd.SampleDesc.Count==1&&
     dd.MipLevels==1&&dd.DepthOrArraySize==1&&!(dd.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&
     (depthView==DXGI_FORMAT_R32_FLOAT||depthView==DXGI_FORMAT_R16_UNORM||depthView==DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
 const auto ed=f.exposure?f.exposure->GetDesc():D3D12_RESOURCE_DESC{};
 const bool useExposure=colorContract.relative&&settings.useGameExposure&&f.createFlagsKnown&&!f.autoExposure&&
     f.exposure&&ed.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&ed.Width&&ed.Height&&ed.MipLevels==1&&
     ed.SampleDesc.Count==1&&ed.DepthOrArraySize==1&&nrb::input_exposure_format(ed.Format)&&
     std::isfinite(f.preExposure)&&f.preExposure>0&&std::isfinite(f.exposureScale)&&f.exposureScale>0;
 // Recordings outlive their GPU work until the game releases the list, and engines
 // that rotate command lists keep several alive at once. Bound what is actually in
 // flight against the runtime's ring, and keep a generous total bound so a rotating
 // pool does not turn every third frame into a bypass.
 // The runtime owns a ring of three outputs (status.json slots_total). Capping the host
 // at two in flight looked right but rejects more than half the frames once the title
 // runs a normal two-deep queue: the in-game capture shows 83 of 150 change lines as
 // "recording pool busy", so NR flickers on and off. The stall is prevented by the
 // single outstanding wait (see gateFence), not by starving the runtime, so the bound
 // stays at the ring size.
 size_t inFlight=0;
 for(const auto& job:p->jobs) if(job!=p->pending&&!job->Done()) ++inFlight;
 if(p->pending)++inFlight;
 if(inFlight>=3||p->jobs.size()>=8){
  p->status="intel: recording pool busy ("+std::to_string(inFlight)+" in flight, "+
   std::to_string(p->jobs.size())+" retained); original input";
  return nullptr;
 }
 auto j=std::make_shared<IntelJob>(p);j->source=f.colour;j->motion=f.motion;j->sourceState=f.colourState;
 auto outDesc=desc;outDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;outDesc.MipLevels=1;
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
 if(FAILED(p->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&outDesc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&j->output))))return nullptr;
 if(!p->inputShaders.Ensure(p->device.Get(),(p->directory/L"nr_hdr_proxy.hlsl").wstring())){
  p->status="intel: full Intel input-codec package required; input shader unavailable ("+p->inputShaders.error+"); original input";return nullptr;
 }
 p->RefreshPanel();
 // The project's control panel wins while its file exists: it is the live remote
 // for network, processing height and look, and the Ins menu owns the same keys in
 // OptiScaler.ini. Without the file the ini/menu values are used unchanged.
 const bool usePanel=p->panel.present;
 if(usePanel&&!p->panel.enabled){p->status="intel: control panel has NR off; original input";return nullptr;}
 const auto& in=settings.intel;
 const uint32_t cHeight=usePanel?p->panel.inputHeight:in.inputHeight;
 const uint32_t cStyle=usePanel?p->panel.style:in.style;
 const float cIntensity=usePanel?p->panel.intensity:in.intensity;
 const float cTone=usePanel?p->panel.tone:in.tone;
 const float cStructure=usePanel?p->panel.structure:in.structure;
 const float cSkin=usePanel?p->panel.skin:in.skin;
 const float cDisplay=usePanel?p->panel.displayStrength:in.displayStrength;
 const bool cAutoMask=usePanel?p->panel.autoMask:in.autoMask;
 const bool cSkinEnabled=usePanel?p->panel.skinEnabled:in.skinEnabled;
 p->lastSource=usePanel?"panel":"ini";
 j->model=usePanel?p->panel.model:int(in.model);p->lastHeight=cHeight;
 // Never switch network while an earlier NR execution is still on the GPU: the
 // component's shutdown has to retire in-flight Vulkan work, and doing that under a
 // live composite is what left the queue without a completing fence (the observed
 // hang after picking Small). The frame is served with the original colour and the
 // switch happens on the first frame with nothing in flight.
 if(p->processor.process&&p->model!=j->model){
  size_t busy=0;
  for(const auto& job:p->jobs) if(!job->Done()) ++busy;
  if(busy){p->status="intel: runtime rebuilt";return nullptr;}
 }
 j->shadowHold=std::clamp(in.shadowHold,0.0f,1.0f);
 j->knee=in.knee>0.05f?in.knee:0.05f;
 j->sameFrame=true; // Shared split path consumes the same frame, never an aged pending result.
 const float motionScaleX=nrb::ngx_motion_to_render_pixels(f.motionScaleX,f.width,motionRect.width);
 const float motionScaleY=nrb::ngx_motion_to_render_pixels(f.motionScaleY,f.height,motionRect.height);
 p->inputDescription=std::string(colorContract.encoding==3?"sRGB":colorContract.encoding==5?"gamma22":colorContract.encoding==1?"linear":colorContract.encoding==4?"PQ2020":"PQ")+
     (colorContract.relative?" relative":" calibrated")+(!colorContract.hdr?"/SDR":colorContract.curve?"/Display":"/Divisor")+
     (useExposure?"/game exposure":"/content")+
     (!f.featureIdentity?"; history reset: unknown feature":!useGameMotion?"; history reset: missing/unknown/jittered guide":"; motion/depth history");
 j->inputIdentity={f.featureIdentity,colorRect,motionRect,depthRect,uint32_t(desc.Format),uint32_t(md.Format),uint32_t(dd.Format),
     UINT(dd.Width),dd.Height,colorContract,motionScaleX,motionScaleY,useGameMotion&&f.featureIdentity!=0,f.depthInverted,uint32_t(useExposure)};
 j->inputToken=++p->inputToken;
 const Impl::NormKey normKey{f.featureIdentity,colorContract.encoding,colorContract.curve,std::bit_cast<uint32_t>(colorContract.unitNits)};
 auto stream=f.featureIdentity?p->normalization[normKey]:nullptr;
 if(!stream){stream=std::make_shared<Impl::NormStream>();stream->gpu=nrb::input_texture(p->device.Get(),2,1,DXGI_FORMAT_R32G32B32A32_FLOAT);}
 if(!stream->gpu)return nullptr;
 if(f.featureIdentity)p->normalization[normKey]=stream;
 j->normStream=stream;j->gameReset=f.reset;
 nrb::IntelInputConstants ic{};
 ic.width=f.width;ic.height=f.height;ic.sourceX=f.colourX;ic.sourceY=f.colourY;
 ic.sourceWidth=f.width;ic.sourceHeight=f.height;ic.motionWidth=motionRect.width;ic.motionHeight=motionRect.height;
 ic.motionX=f.motionX;ic.motionY=f.motionY;ic.motionScaleX=motionScaleX;ic.motionScaleY=motionScaleY;
 ic.encoding=colorContract.encoding;ic.curve=colorContract.curve;ic.hdr=colorContract.hdr;ic.feed=useExposure?1u:0u;
 ic.divisor=colorContract.divisor;ic.preExposure=f.preExposure;ic.exposureScale=f.exposureScale;
 ic.strength=std::clamp(cDisplay,0.0f,2.0f);ic.shadowFloor=j->shadowHold;ic.shadowRange=.15f;ic.hdrKnee=j->knee;
 ic.depthWidth=depthRect.width;ic.depthHeight=depthRect.height;ic.depthX=f.depthX;ic.depthY=f.depthY;ic.useDepth=useDepth;
 ic.chromaClamp=1;ic.token=j->inputToken;ic.useMotion=useGameMotion;ic.governor=3;
 const float dt=stream->submittedAt?std::clamp(float(GetTickCount64()-stream->submittedAt)/1000.f,0.f,.1f):1.f/60.f;
 ic.attack=4.f*dt;ic.release=.5f*dt;ic.snap=f.reset&&(!stream->resetAt||GetTickCount64()-stream->resetAt>=250)?1.f:0.f;
 j->hdr=std::make_unique<nrb::IntelInputAdapter>();
 if(!j->hdr->Initialize(&p->inputShaders,f.colour,useGameMotion?f.motion:nullptr,useExposure?f.exposure:nullptr,useDepth?f.depth:nullptr,useDepth?depthView:DXGI_FORMAT_R32_FLOAT,j->output.Get(),stream->gpu.Get(),ic)){
  p->status="intel: typed shader load/store unsupported for source format; original input";return nullptr;
 }
 auto& n=j->frame;n.abi_size=sizeof(n);n.device=p->device.Get();n.color=j->hdr->proxy_color();n.motion=j->hdr->nr_motion();n.depth=j->hdr->nr_depth();
 n.route=NRB_ROUTE_DLSS;n.route_evidence=NRB_EVIDENCE_GAME_NGX;
 n.render_width=f.width;n.render_height=f.height;n.output_width=f.width;n.output_height=f.height;
 // The adapter has already normalized colour and converted NGX motion into
 // render pixels. Jittered motion is not declared supported by this proxy ABI.
 n.motion_scale_x=1;n.motion_scale_y=1;n.motion_scale_origin=NRB_MV_DLSS_NGX;
 n.pre_exposure=1;n.exposure_scale=1;n.auto_exposure=0;n.hdr_input=0;
 n.low_resolution_motion=1;n.reset_history=f.reset;n.jitter_x=0;n.jitter_y=0;
 n.motion_jittered=0;n.depth_inverted=f.depthInverted;
 n.controls={sizeof(NRB_Controls),1,cHeight,cStyle,useGameMotion&&f.featureIdentity?NRB_HISTORY_FUSED:NRB_HISTORY_RESET,1,
  uint32_t(cAutoMask?1u:0u),uint32_t(cSkinEnabled?1u:0u),cDisplay,
  cIntensity,cTone,cStructure,cSkin};
 auto lease=AttachIntelRecording(j,logical.Get());
 if(!lease)return nullptr;p->jobs.push_back(j);
 resources->BeginPrivateBindings();
 // Valid fallback is recorded on every execution, including runtime failure.
 Transition(cmd,f.colour,f.colourState,D3D12_RESOURCE_STATE_COPY_SOURCE);
 Transition(cmd,j->output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
 cmd->CopyResource(j->output.Get(),f.colour);
 Transition(cmd,j->output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 Transition(cmd,f.colour,D3D12_RESOURCE_STATE_COPY_SOURCE,f.colourState);
 // Entry/exit states match on every actual execution and retained replay.
 Transition(cmd,j->hdr->proxy_color(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 Transition(cmd,j->hdr->nr_motion(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 p->lastMotion=useGameMotion;p->lastMotionScaleX=f.motionScaleX;p->lastMotionScaleY=f.motionScaleY;
 p->lastMotionScaleXUsed=motionScaleX;p->lastMotionScaleYUsed=motionScaleY;
 Transition(cmd,j->hdr->nr_depth(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 j->hdr->RecordPrepare(cmd,f.colourState,f.motionState,f.exposureState,f.depthState);
 resources->EndPrivateBindings();
 if(FAILED(logical->SplitSegments())){p->status="intel: split failed";return nullptr;}
 lease->ready=true;
 // No transient status here: the panel would flicker between "recorded" and the
 // real outcome every frame. The outcome is published once the frame completes.
 p->sceneOutput=j->output;
 return j->output.Get();
}
void IntelBackend::Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*){if(!g_insideProcess)LmxxfRecording::Collect();PollRelease();}
bool IntelBackend::PollRelease(){std::lock_guard lock(Submission::RecordingMutex());p->LogLongFrame(GetTickCount64());p->Collect();
 // A pending output keeps its job alive on purpose, so releasing has to drop it first
 // or the runtime shutdown below would never find an empty queue.
 if(p->release&&p->pending){
  auto dropped=p->pending;p->pending.reset();p->Collect();
  // Shutdown frees every slot anyway, so an output the runtime did not retire must not
  // keep the queue non-empty either; release it the way a replayed recording is.
  if(dropped&&!dropped->retired){dropped->submitted=false;dropped->ReleaseComposite();p->Collect();}
 }
 if(p->release&&p->jobs.empty()&&p->processor.process){if(p->active->shutdown()!=0)return false;p->processor={};p->model=-1;}return true;}
void IntelBackend::ReleaseSession(){std::lock_guard lock(Submission::RecordingMutex());p->release=true;p->reset=true;PollRelease();}
bool IntelBackend::Shutdown(){ReleaseSession();return p->jobs.empty()&&!p->processor.process;}
void IntelBackend::InvalidateHistory(){std::lock_guard lock(Submission::RecordingMutex());p->reset=true;}
std::string IntelBackend::Status()const {
 std::lock_guard lock(Submission::RecordingMutex());p->RefreshActual();
 // Transient state first, then the last completed frame's contract: a rejected
 // Record must not hide the guides the running frames actually used.
 std::string reported=p->outcome.empty()||p->outcome==p->status?p->status:p->status+" | last: "+p->outcome;
 // A rejected frame overwrites the transient state; keep the last failure visible
 // while the host is still bypassing, or the panel would only ever show "recorded".
 if(!p->lastFailure.empty())reported+=""+p->lastFailure;
 return reported;
}
bool IntelBackend::IsRunning()const {std::lock_guard lock(Submission::RecordingMutex());return p->completed>0&&!p->release;}
}
