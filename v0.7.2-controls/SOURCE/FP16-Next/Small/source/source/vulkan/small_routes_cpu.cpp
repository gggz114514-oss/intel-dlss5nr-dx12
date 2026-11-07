#include "port_network_bridge_backend.cpp"
#include <iostream>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void dump(const Plan& p,const char* mode,NRSize size){
 std::cout<<"CASE "<<mode<<" "<<size.w<<" "<<size.h<<"\n";
 std::cout<<"PORT_NETWORK_1 "<<kPlanRows<<" "<<p.buffers.size()<<" "<<p.passes.size()<<" "<<p.outputs.size()<<"\n";
 for(const auto& b:p.buffers)std::cout<<"BUFFER "<<std::quoted(b.name)<<" "<<b.bytes<<" "<<std::quoted(std::filesystem::path(b.path).generic_string())<<"\n";
 for(const auto& a:p.passes){std::cout<<"PASS "<<a.row<<" "<<std::quoted(a.name)<<" "<<std::quoted(std::filesystem::path(a.spv).generic_string())<<" "<<a.subgroup<<" "<<a.shared<<" "<<a.bindings.size()<<" "<<a.push.size()<<" "<<a.gx<<" "<<a.gy<<" "<<a.gz<<"\n";
  for(const auto& x:a.bindings)std::cout<<"BIND "<<x.binding<<" "<<x.resource<<" "<<x.offset<<" "<<x.bytes<<"\n";
  std::cout<<"PUSH";for(auto x:a.push)std::cout<<" "<<x;std::cout<<"\n";}
 for(const auto& x:p.outputs)std::cout<<"OUTPUT "<<x.resource<<" "<<x.dtype<<" "<<std::quoted(x.file)<<"\n";
 for(size_t i=0;i<p.buffers.size();++i){const auto& b=p.buffers[i];std::cout<<"POLICY "<<i<<" "<<b.resident<<" "<<b.firstPass<<" "<<b.lastPass<<" "<<b.clearFirst<<"\n";}
 for(const auto& h:p.history)std::cout<<"HISTORY "<<h.source<<" "<<h.destination<<" "<<h.sourceOffset<<" "<<h.destinationOffset<<" "<<h.bytes<<" "<<h.controlsOffset<<"\n";
}
int main(int argc,char** argv){try{
 require(argc==2,"manifest argument");const auto original=checked_plan(argv[1]);
 for(auto size:{NRSize{1280,720},NRSize{320,320},NRSize{640,360},NRSize{853,480},NRSize{854,480},NRSize{960,540},NRSize{1114,626},NRSize{1485,835},NRSize{1707,960},NRSize{1920,1080},NRSize{2227,1253},NRSize{2560,1440},NRSize{3840,2160},NRSize{1024,576}}){
  for(bool diagnostic:{false,true}){_putenv_s("NR_DIAGNOSTIC_DEBUG",diagnostic?"1":"0");auto p=original;auto g=nr_geometry(size,size,size);reshape_plan(p,g);
   require(p.passes.size()==kPlanPasses&&p.buffers.size()==kPlanBuffers,"model graph count");
   for(const auto& a:p.passes){require(a.gx&&a.gy&&a.gz&&a.gx<=65535&&a.gy<=65535&&a.gz<=65535,"grid domain");for(const auto& x:a.bindings)require(x.offset+x.bytes<=p.buffers[x.resource].bytes,"bind bounds");}
   dump(p,diagnostic?"diagnostic":"normal",size);
   if(!diagnostic){auto before=p;share_production_inputs(p,g);route_production_history(p,g,argv[1]);const auto& h=p.history.front();
    require(p.history.size()==1&&p.buffers[before.history[1].destination].production_unused,"post removed/private1history");
    require(h.source!=h.destination&&p.buffers[h.source].resident&&p.buffers[h.destination].resident,"persistent disjoint banks");
    require(h.sourceOffset+h.bytes<=p.buffers[h.source].bytes&&h.destinationOffset+h.bytes<=p.buffers[h.destination].bytes,"bank bounds");
    require(h.destinationOffset>=g.network.pixels()*16&&h.destinationOffset+h.bytes<=uint64_t(g.motion)*4,"pre history excludes color/controls/motion");
    dump(p,"production",size);
   }
  }
 }
 std::cout<<"ACTUAL_SMALL_ROUTES_CPU_PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
