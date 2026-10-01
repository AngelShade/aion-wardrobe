// Compile with MSVC /std:c++17 /EHsc. Isolated native widget contract fixture;
// never loads or modifies a running game or its equipment.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <cmath>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
using Ptr=void*;
uint8_t* fixture_game=nullptr;
HMODULE fixture_module(const wchar_t*){return reinterpret_cast<HMODULE>(fixture_game);}
void log(const char* message){std::cout<<message<<'\n';}
template<typename T>T symbol(HMODULE,const char*){return nullptr;}
void* trampoline(void* p,size_t){return p;}
void detour(void*,void*,size_t){}
size_t to_utf8(Ptr p,char* out,size_t n){auto& s=*static_cast<std::string*>(p);size_t copy=(std::min)(s.size(),n-1);std::memcpy(out,s.data(),copy);out[copy]=0;return copy;}
Ptr from_wide(const wchar_t* value,size_t count){return new std::string(value,value+count);}
void destroy_string(Ptr value){delete static_cast<std::string*>(value);}
#define GetModuleHandleW fixture_module
#include "wardrobe_preview.h"
#undef GetModuleHandleW

struct Widget { std::array<uint8_t,0xa00> bytes{}; std::string name; uint32_t type; };
std::unordered_map<Ptr,Widget*> widgets;
std::array<Ptr,0x500/8> vtable{};
std::unordered_map<Ptr,std::vector<Ptr>> children;
Widget *host,*browser,*paper,*normal,*zoom,*robot;
int resets=0;std::vector<uint32_t> appearances;
const char* __cdecl name(Ptr p){return widgets.at(p)->name.c_str();}
Ptr __cdecl lookup(Ptr p,const char* n,uint32_t type){for(Ptr c:children[p])if(widgets.at(c)->name==n&&widgets.at(c)->type==type)return c;return nullptr;}
void __cdecl set_rect(Ptr p,const wardrobe::Rect* r){wardrobe::field<wardrobe::Rect>(p,0x50)=*r;}
void __cdecl absolute_rect(Ptr p,wardrobe::Rect* r){*r=wardrobe::rect(p);}
void __cdecl add(Ptr p,Ptr c,int){children[p].push_back(c);}
void __cdecl remove_child(Ptr p,Ptr c){auto& a=children[p];a.erase(std::remove(a.begin(),a.end(),c),a.end());}
void __cdecl set_flag(Ptr p,uint64_t bits,int){
 wardrobe::field<uint64_t>(p,0x30)|=bits;
 // Stock OnVisible restores the saved popup rectangle.
 if(p==paper->bytes.data()&&(bits&1))wardrobe::field<wardrobe::Rect>(p,0x50)={20,140,320,490};
}
void __cdecl clear_flag(Ptr p,uint64_t bits,int){wardrobe::field<uint64_t>(p,0x30)&=~bits;}
void __cdecl reset(Ptr p,bool){assert(p==paper->bytes.data());resets++;appearances.clear();wardrobe::field<uint64_t>(normal->bytes.data(),0x30)|=1;wardrobe::field<int>(p,0x598)=1;}
void __cdecl appearance(Ptr p,const uint32_t* data){assert(p==paper->bytes.data());appearances.push_back(data[0]);}
void __cdecl zoom_change(Ptr p,int value){wardrobe::field<int>(p,0x57c)=value;}
void __cdecl noop(Ptr){}
void __cdecl model_reload(Ptr p){assert(p==paper->bytes.data());for(auto w:{normal,zoom,robot})wardrobe::field<Ptr>(w->bytes.data(),0x470)=w;}
void __cdecl model_camera(Ptr p){
 auto& camera=wardrobe::field<std::array<float,3>>(p,0x6e8);
 camera=p==zoom->bytes.data()?std::array<float,3>{-.02f,.8f,.55f}:std::array<float,3>{0,5,0};
 wardrobe::field<std::array<float,3>>(p,0x8bc)=camera;
}
double __cdecl fixture_fmod(double x,double y){return std::fmod(x,y);}
double __cdecl fixture_sqrt(double x){return std::sqrt(x);}
struct Value {std::string text;int number=0;};
size_t __cdecl count(Ptr a){return static_cast<std::vector<Value>*>(a)->size();}
Ptr __cdecl element(Ptr a,size_t i){return &static_cast<std::vector<Value>*>(a)->at(i);}
Ptr __cdecl text(Ptr v){return new std::string(static_cast<Value*>(v)->text);}
int __cdecl integer(Ptr v){return static_cast<Value*>(v)->number;}
std::string callback_url="http://127.0.0.1:8091/market/wardrobe?session_id=fixture",callback_script;
Ptr __cdecl view_url(Ptr){return new std::string(callback_url);}
void __cdecl execute_js(Ptr,Ptr script,Ptr){callback_script=*static_cast<std::string*>(script);}
void emit(size_t offset,Ptr function){uint8_t jump[12]={0x48,0xb8};std::memcpy(jump+2,&function,8);jump[10]=0xff;jump[11]=0xe0;std::memcpy(fixture_game+offset,jump,sizeof(jump));}
Widget make(const char* n,uint32_t type,uint32_t id,wardrobe::Rect rect,uint64_t flags){Widget w;w.name=n;w.type=type;Ptr vt=vtable.data();std::memcpy(w.bytes.data(),&vt,8);wardrobe::field<uint32_t>(w.bytes.data(),0x340)=id;wardrobe::field<wardrobe::Rect>(w.bytes.data(),0x50)=rect;wardrobe::field<uint64_t>(w.bytes.data(),0x30)=flags;return w;}
int main(int argc,char** argv){
 std::cout<<std::unitbuf;
 AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* e)->LONG{
  if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){std::cerr<<"Native CPU fixture fault at RVA "<<std::hex<<(e->ContextRecord->Rip-reinterpret_cast<uintptr_t>(fixture_game))<<", address "<<e->ExceptionRecord->ExceptionInformation[1]<<"; stack";for(int i=0;i<12;++i)std::cerr<<' '<<(reinterpret_cast<uintptr_t*>(e->ContextRecord->Rsp)[i]-reinterpret_cast<uintptr_t>(fixture_game));std::cerr<<std::dec<<'\n';}
  return EXCEPTION_CONTINUE_SEARCH;
 });
 if(argc!=2){std::cerr<<"Pass the matching original Game.dll path. The camera/rotation checks execute its real CPU math.\n";return 2;}
 std::ifstream binary(argv[1],std::ios::binary);std::vector<uint8_t> original((std::istreambuf_iterator<char>(binary)),{});
 assert(original.size()>0x1400000);
 auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(original.data()+reinterpret_cast<const IMAGE_DOS_HEADER*>(original.data())->e_lfanew);
 assert(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64);
 fixture_game=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(fixture_game);
 auto section=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){assert(section[i].PointerToRawData+section[i].SizeOfRawData<=original.size());std::memcpy(fixture_game+section[i].VirtualAddress,original.data()+section[i].PointerToRawData,section[i].SizeOfRawData);}
 assert(fixture_game[0x487480]==0x40 && fixture_game[0x487481]==0x53 && fixture_game[0x1bf0d0]==0x44);
 std::cout<<"Original native CPU functions mapped\n";
 vtable[0xa8/8]=reinterpret_cast<Ptr>(name);vtable[0x338/8]=reinterpret_cast<Ptr>(lookup);vtable[0x1a8/8]=reinterpret_cast<Ptr>(set_rect);
 vtable[0x58/8]=reinterpret_cast<Ptr>(absolute_rect);
 vtable[0x2f8/8]=reinterpret_cast<Ptr>(add);vtable[0x300/8]=reinterpret_cast<Ptr>(remove_child);vtable[0xd0/8]=reinterpret_cast<Ptr>(set_flag);vtable[0xd8/8]=reinterpret_cast<Ptr>(clear_flag);
 auto h=make("PrivateWardrobe",0x2003,0x211,{0,0,1920,1080},1),b=make("PrivateWardrobeBrowser",0x2012,0,{0,28,1920,1052},1),p=make("paper_doll_dialog",0x2003,0x110,{20,140,320,490},0);
 auto n=make("char_model",0x2000,0,{1,0,318,426},1),z=make("char_model_zoomin",0x2000,0,{1,0,318,426},0),r=make("robot_model",0x2000,0,{1,0,318,426},0);
 host=&h;browser=&b;paper=&p;normal=&n;zoom=&z;robot=&r;
 for(Widget* w:{host,browser,paper,normal,zoom,robot})widgets[w->bytes.data()]=w;
 children[h.bytes.data()]={b.bytes.data()};children[p.bytes.data()]={n.bytes.data(),z.bytes.data(),r.bytes.data()};
 for(Widget* w:{normal,zoom,robot})wardrobe::field<Ptr>(w->bytes.data(),0x290)=p.bytes.data();
 for(Widget* w:{normal,zoom,robot}){
  wardrobe::field<std::array<float,3>>(w->bytes.data(),0x6b8+0x64)={1,1,1};
  wardrobe::field<float>(w->bytes.data(),0x6b8+0x60)=0.6f;
  wardrobe::field<float>(w->bytes.data(),0x6b8+0x78)=1.32f;
  wardrobe::field<float>(w->bytes.data(),0x6b8+0x80)=.25f;
  wardrobe::field<float>(w->bytes.data(),0x6b8+0x98)=1024;
  wardrobe::field<std::array<float,3>>(w->bytes.data(),0x6e8)={0,0,0}; // unopened native view
 }
 for(size_t off:{size_t(0x528),size_t(0x530),size_t(0x538)})wardrobe::field<Ptr>(p.bytes.data(),off)=off==0x528?n.bytes.data():off==0x530?z.bytes.data():r.bytes.data();
 *reinterpret_cast<Ptr*>(fixture_game+0x13875c0+0x211*8)=h.bytes.data();*reinterpret_cast<Ptr*>(fixture_game+0x13875c0+0x110*8)=p.bytes.data();
 emit(0x8053f0,reinterpret_cast<Ptr>(reset));emit(0x804bd0,reinterpret_cast<Ptr>(appearance));emit(0x805bd0,reinterpret_cast<Ptr>(zoom_change));emit(0x805cc0,reinterpret_cast<Ptr>(noop));emit(0x805d40,reinterpret_cast<Ptr>(noop));
 emit(0x8046f0,reinterpret_cast<Ptr>(model_reload));emit(0x48e340,reinterpret_cast<Ptr>(model_camera));
 // Only imported CRT math is substituted; camera/frustum and yaw code is the
 // original client machine code. Import thunks are six bytes apart.
 *reinterpret_cast<Ptr*>(fixture_game+0xb7fcb0)=reinterpret_cast<Ptr>(fixture_sqrt);
 *reinterpret_cast<Ptr*>(fixture_game+0xb80348)=reinterpret_cast<Ptr>(fixture_fmod);
 wardrobe::array_size=count;wardrobe::array_element=element;wardrobe::value_string=text;wardrobe::value_integer=integer;wardrobe::view_url=view_url;wardrobe::execute_js=execute_js;
 wardrobe::tick();
 std::vector<Value> args={{"114100001,110100001"},{"",700},{"",150},{"",700},{"",550},{"",1}};
 wardrobe::Command command;assert(wardrobe::parse_preview(&args,command));
 assert(wardrobe::preview(command));assert(resets==1&&appearances==std::vector<uint32_t>({114100001,110100001}));
 for(Widget* w:{normal,zoom,robot}){assert(wardrobe::field<Ptr>(w->bytes.data(),0x290)==h.bytes.data());auto rr=wardrobe::rect(w->bytes.data());assert(rr.x==700&&rr.y==178&&rr.w==700&&rr.h==550);}
 assert(wardrobe::rect(p.bytes.data()).x==-10000);
 wardrobe::control("visible",0);assert(!(wardrobe::field<uint64_t>(n.bytes.data(),0x30)&1));wardrobe::control("visible",1);assert(wardrobe::field<uint64_t>(n.bytes.data(),0x30)&1);assert(!(wardrobe::field<uint64_t>(z.bytes.data(),0x30)&1));
 bool off_thread=true;std::thread thread([&]{off_thread=wardrobe::preview(command);});thread.join();assert(!off_thread&&resets==1);
 args[0].text="bad";assert(!wardrobe::parse_preview(&args,command)&&resets==1);args[0].text="114100001";assert(wardrobe::parse_preview(&args,command));
 wardrobe::visible(h.bytes.data(),0);assert(!wardrobe::paper);
 for(Widget* w:{normal,zoom,robot}){assert(wardrobe::field<Ptr>(w->bytes.data(),0x290)==p.bytes.data());assert(wardrobe::rect(w->bytes.data()).w==318);}
 assert(!(wardrobe::field<uint64_t>(p.bytes.data(),0x30)&1)&&wardrobe::rect(p.bytes.data()).x==20);
 assert(wardrobe::preview(command)&&resets==2&&appearances.size()==1);
 std::string object="AionObject",method="WardrobePreview";
 // Actual browser callbacks execute on a worker; no native model access until
 // the game event pump consumes the independent argument copies.
 std::thread worker([&]{wardrobe::callback(nullptr,&object,&method,&args);});worker.join();assert(resets==2&&callback_script.empty());
 args[0].text="110100001";wardrobe::tick();assert(resets==3&&appearances[0]==114100001);
 std::string poll="WardrobePoll";std::vector<Value> empty;
 std::thread poller([&]{wardrobe::callback(nullptr,&object,&poll,&empty);});poller.join();assert(callback_script.find("State(true,1)")!=std::string::npos);
 args[0].text="bad";args[5].number=2;wardrobe::callback(nullptr,&object,&method,&args);wardrobe::tick();wardrobe::callback(nullptr,&object,&poll,&empty);assert(resets==3&&callback_script.find("State(false,2)")!=std::string::npos);
 callback_script.clear();callback_url="https://example.invalid/market/wardrobe";wardrobe::callback(nullptr,&object,&method,&args);assert(resets==3&&callback_script.empty());
 callback_url="http://127.0.0.1:8091/market/wardrobe";args[0].text="114100001";
 for(int i=3;i<=500;++i){args[5].number=i;wardrobe::callback(nullptr,&object,&method,&args);}
 assert(wardrobe::commands.size()==1);wardrobe::tick();assert(resets==4);
 wardrobe::callback(nullptr,&object,&poll,&empty);assert(callback_script.find("State(true,500)")!=std::string::npos);
 std::string ctl="WardrobeControl";std::vector<Value> rotation={{"left"},{"",1}},release={{"left"},{"",0}};
 wardrobe::callback(nullptr,&object,&ctl,&rotation);wardrobe::callback(nullptr,&object,&ctl,&release);wardrobe::tick();assert(wardrobe::field<int>(p.bytes.data(),0x570)==0);
 args[5].number=501;wardrobe::callback(nullptr,&object,&method,&args);wardrobe::destroy(nullptr);wardrobe::tick();assert(resets==4); // view destroyed while request queued
 wardrobe::field<int>(p.bytes.data(),0x57c)=1;wardrobe::control("wings",1);
 assert(wardrobe::field<int>(p.bytes.data(),0x578)==2 && wardrobe::field<int>(p.bytes.data(),0x57c)==0 && wardrobe::field<int>(p.bytes.data(),0x598)==1);
 wardrobe::control("wings",0);assert(wardrobe::field<int>(p.bytes.data(),0x578)==0);
 wardrobe::callback(nullptr,&object,&method,&args);wardrobe::tick();assert(resets==5); // reused address has a fresh state
 std::string stock="ItemPreview";callback_url="http://127.0.0.1:8091/shop";wardrobe::callback(nullptr,&object,&stock,&args);wardrobe::tick();assert(!wardrobe::paper);
 callback_url="http://127.0.0.1:8091/market/wardrobe";
 std::vector<Value> modal={{"visible"},{"",0}};
 wardrobe::callback(nullptr,&object,&ctl,&modal);wardrobe::callback(nullptr,&object,&method,&args);wardrobe::tick();assert(wardrobe::modal_hidden && !(wardrobe::field<uint64_t>(n.bytes.data(),0x30)&1));
 wardrobe::callback(nullptr,&object,&method,&args);wardrobe::tick();assert(!(wardrobe::field<uint64_t>(n.bytes.data(),0x30)&1)); // native reset cannot draw over a confirmation
 modal[1].number=1;wardrobe::callback(nullptr,&object,&ctl,&modal);wardrobe::tick();assert(wardrobe::field<uint64_t>(n.bytes.data(),0x30)&1);
 assert(wardrobe::preview(command));
 wardrobe::tick();assert(wardrobe::moved[0].camera_ready && wardrobe::moved[0].base_camera[1]==5);
 // Real hidden Win32 window messages exercise hit testing, capture/release,
 // screen-to-client wheel coordinates and game-thread camera application.
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WardrobeMouseFixture";assert(RegisterClassW(&wc));
 HWND hwnd=CreateWindowW(wc.lpszClassName,L"fixture",WS_POPUP,80,90,1920,1080,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 wardrobe::mouse::window=hwnd;wardrobe::mouse::prior=DefWindowProcW;SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(wardrobe::mouse::receive));wardrobe::tick();
 SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(800,300));SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(900,300));SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(900,300));
 assert(wardrobe::field<float>(n.bytes.data(),0x65c)==0);wardrobe::tick();assert(wardrobe::field<float>(n.bytes.data(),0x65c)==45 && wardrobe::mouse::dragging==0);
 wardrobe::mouse::apply(-200,0,0,0);assert(wardrobe::field<float>(n.bytes.data(),0x65c)==315);
 wardrobe::mouse::apply(200,0,0,0);assert(wardrobe::field<float>(n.bytes.data(),0x65c)==45);
 POINT wheelPoint{900,300};ClientToScreen(hwnd,&wheelPoint);
 auto beforeFrustum=wardrobe::field<std::array<float,64>>(n.bytes.data(),0x6b8+0x100);
 SendMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,120),MAKELPARAM(wheelPoint.x,wheelPoint.y));wardrobe::tick();assert(wardrobe::field<float>(n.bytes.data(),0x6ec)<5);
 assert((wardrobe::field<std::array<float,64>>(n.bytes.data(),0x6b8+0x100)!=beforeFrustum)); // real camera planes, not just XYZ
 SendMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,-120),MAKELPARAM(wheelPoint.x,wheelPoint.y));wardrobe::tick();assert(std::abs(wardrobe::field<float>(n.bytes.data(),0x6ec)-5)<0.001);
 SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(800,300));SendMessageW(hwnd,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(900,350));SendMessageW(hwnd,WM_RBUTTONUP,0,MAKELPARAM(900,350));wardrobe::tick();assert(wardrobe::field<float>(n.bytes.data(),0x6e8)<0 && wardrobe::field<float>(n.bytes.data(),0x6f0)>0);
 auto cameraAfterDrag=wardrobe::field<std::array<float,3>>(n.bytes.data(),0x6e8);float headingAfterDrag=wardrobe::field<float>(n.bytes.data(),0x65c);
 wardrobe::field<float>(n.bytes.data(),0x65c)=90;wardrobe::tick();assert(wardrobe::moved[0].preview_angle[2]==90);
 headingAfterDrag=90; // Stock Left/Right changes must survive the next Try On.
 wardrobe::preview(command);wardrobe::tick();assert((wardrobe::field<std::array<float,3>>(n.bytes.data(),0x6e8)==cameraAfterDrag) && wardrobe::field<float>(n.bytes.data(),0x65c)==headingAfterDrag);
 wardrobe::field<std::array<float,3>>(n.bytes.data(),0x6e8)={0,0,0};wardrobe::mouse::retry_after=0;wardrobe::tick();assert((wardrobe::field<std::array<float,3>>(n.bytes.data(),0x6e8)==cameraAfterDrag)); // camera invalidated by zone transition
 POINT outside{50,50};ClientToScreen(hwnd,&outside);float previous=wardrobe::mouse::scale;SendMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,120),MAKELPARAM(outside.x,outside.y));wardrobe::tick();assert(wardrobe::mouse::scale==previous);
 wardrobe::control("visible",0);wardrobe::tick();SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(800,300));assert(wardrobe::mouse::dragging==0);wardrobe::control("visible",1);
 wardrobe::mouse::apply(0,0,0,100);assert(wardrobe::mouse::scale==0.35f);wardrobe::mouse::apply(0,0,0,-100);assert(wardrobe::mouse::scale==2.5f);wardrobe::control("camera-reset",1);assert(wardrobe::mouse::scale==1 && wardrobe::field<float>(n.bytes.data(),0x6ec)==5 && wardrobe::field<float>(n.bytes.data(),0x65c)==0);
 DestroyWindow(hwnd);assert(!wardrobe::mouse::window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
 wardrobe::visible(p.bytes.data(),-1);assert(!wardrobe::paper);
 // The journey browser fills the viewport on the UI thread, including resize.
 auto journey=make("PrivateJourney",0x2003,0x212,{5,5,1280,960},0),journeyBrowser=make("PrivateJourneyBrowser",0x2012,0,{0,0,1280,960},1);
 widgets[journey.bytes.data()]=&journey;widgets[journeyBrowser.bytes.data()]=&journeyBrowser;
 children[journey.bytes.data()]={journeyBrowser.bytes.data()};
 *reinterpret_cast<Ptr*>(fixture_game+0x13875c0+0x212*8)=journey.bytes.data();
 *reinterpret_cast<double*>(fixture_game+0x1378ea8)=1920;*reinterpret_cast<double*>(fixture_game+0x1378eb0)=1080;
 callback_url="http://127.0.0.1:8091/journey?session_id=fixture";
 std::string journeyMethod="JourneyVisibility";std::vector<Value> journeyArgs={{"",1}};
 std::thread journeyWorker([&]{wardrobe::callback(nullptr,&object,&journeyMethod,&journeyArgs);});journeyWorker.join();
 assert(!(wardrobe::field<uint64_t>(journey.bytes.data(),0x30)&1));wardrobe::tick();
 assert(wardrobe::field<uint64_t>(journey.bytes.data(),0x30)&1);
 for(auto w:{&journey,&journeyBrowser}){auto rr=wardrobe::rect(w->bytes.data());assert(rr.x==0 && rr.y==0 && rr.w==1920 && rr.h==1080);}
 *reinterpret_cast<double*>(fixture_game+0x1378ea8)=3440;*reinterpret_cast<double*>(fixture_game+0x1378eb0)=1440;wardrobe::tick();
 assert(wardrobe::rect(journey.bytes.data()).w==3440 && wardrobe::rect(journeyBrowser.bytes.data()).h==1440);
 journeyArgs[0].number=0;wardrobe::callback(nullptr,&object,&journeyMethod,&journeyArgs);wardrobe::tick();assert(!(wardrobe::field<uint64_t>(journey.bytes.data(),0x30)&1));
 callback_url="https://example.invalid/journey";journeyArgs[0].number=1;wardrobe::callback(nullptr,&object,&journeyMethod,&journeyArgs);wardrobe::tick();assert(!(wardrobe::field<uint64_t>(journey.bytes.data(),0x30)&1));
 callback_url="http://127.0.0.1:8091/journey";wardrobe::callback(nullptr,&object,&journeyMethod,&journeyArgs);wardrobe::destroy(nullptr);wardrobe::tick();assert(!(wardrobe::field<uint64_t>(journey.bytes.data(),0x30)&1));
 std::cout<<"PASS: journey local-page and destroyed-view guards; game-thread fullscreen show/hide and viewport resize\n";
 VirtualFree(fixture_game,0,MEM_RELEASE);
 std::cout<<"PASS: unopened cameras initialize before input; original Game.dll rotation and camera/frustum CPU math; Win32 drag/wheel; appearance and invalid-camera recovery; bounds/zoom limits; modal blocking; native ownership and queued-thread guards\n";
}
