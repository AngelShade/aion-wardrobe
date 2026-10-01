// 4.8 NA paper-doll integration. Browser requests are queued for the native
// browser event pump; only that thread accesses the native character widgets.
// The existing native 3dviews are moved, never copied or replaced by a web model.
namespace wardrobe {
using Ptr = void*;
using JSCallback = void(__cdecl*)(Ptr,Ptr,Ptr,Ptr);
using JSSet = void(__cdecl*)(Ptr,JSCallback);
JSSet original_js_set=nullptr;
void(__cdecl* object_callback)(Ptr,Ptr,Ptr)=nullptr;
Ptr(__cdecl* view_url)(Ptr)=nullptr;
size_t(__cdecl* array_size)(Ptr)=nullptr;
Ptr(__cdecl* array_element)(Ptr,size_t)=nullptr;
Ptr(__cdecl* value_string)(Ptr)=nullptr;
int(__cdecl* value_integer)(Ptr)=nullptr;
void(__cdecl* execute_js)(Ptr,Ptr,Ptr)=nullptr;
std::unordered_map<Ptr,JSCallback> prior_js;
DWORD ui_thread=0;
Ptr host=nullptr, paper=nullptr;
bool changing=false;
struct Rect { double x,y,w,h; };
struct Moved {
    Ptr widget; Rect rect; std::array<float,3> camera,angle;
    std::array<float,3> base_camera{},preview_angle{};
    Ptr entity=nullptr; bool camera_ready=false;
};
std::vector<Moved> moved;
Rect paper_rect{};
uint64_t paper_flags=0;
std::array<uint64_t,3> modal_flags{};
bool modal_hidden=false;
const char* last_failure=nullptr;
bool fail(const char* reason){if(last_failure!=reason){log(reason);last_failure=reason;}return false;}
struct State { bool active=true; int completed=0; bool ready=false; bool model_visible=true; };
struct Command {
    std::shared_ptr<State> state;
    int request=0,pressed=0;
    Rect bounds{};
    std::vector<uint32_t> items;
    std::string control;
};
std::mutex queue_mutex;
std::unordered_map<Ptr,std::shared_ptr<State>> states;
std::vector<Command> commands;
bool stock_preview_pending=false;

uint8_t* game(){return reinterpret_cast<uint8_t*>(GetModuleHandleW(L"Game.dll"));}
template<typename T>T& field(Ptr p,size_t offset){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+offset);}
template<typename F>F method(Ptr p,size_t offset){return reinterpret_cast<F>(*reinterpret_cast<void**>(field<uint8_t*>(p,0)+offset));}
void rect(Ptr p,const Rect& r){method<void(__cdecl*)(Ptr,const Rect*)>(p,0x1a8)(p,&r);}
Rect rect(Ptr p){return field<Rect>(p,0x50);}
Ptr lookup(Ptr p,const char* name,uint32_t type){return method<Ptr(__cdecl*)(Ptr,const char*,uint32_t)>(p,0x338)(p,name,type);}
void flag(Ptr p,uint64_t bits,bool set){method<void(__cdecl*)(Ptr,uint64_t,int)>(p,set?0xd0:0xd8)(p,bits,0);}
void move(Ptr widget,Ptr parent){
    Ptr old=field<Ptr>(widget,0x290);
    if(old==parent)return;
    if(old)method<void(__cdecl*)(Ptr,Ptr)>(old,0x300)(old,widget);
    field<Ptr>(widget,0x290)=parent;
    if(parent)method<void(__cdecl*)(Ptr,Ptr,int)>(parent,0x2f8)(parent,widget,0);
}
#include "wardrobe_mouse.h"
Ptr find_host(){
    auto g=game();if(!g)return nullptr;
    for(int id=0x20e;id<=0x221;++id){
        Ptr p=*reinterpret_cast<Ptr*>(g+0x13875c0+id*8);
        if(!p || !(field<uint64_t>(p,0x30)&1))continue;
        const char* n=method<const char*(__cdecl*)(Ptr)>(p,0xa8)(p);
        if(n && !std::strcmp(n,"PrivateWardrobe") && lookup(p,"PrivateWardrobeBrowser",0x2012))return p;
    }
    return nullptr;
}
void detach(bool hide=true){
    if(changing || !paper)return;
    changing=true;
    mouse::stop();
    field<int>(paper,0x570)=0;
    if(modal_hidden)for(size_t i=0;i<moved.size();++i)flag(moved[i].widget,1,(modal_flags[i]&1)!=0);
    for(const auto& m:moved){mouse::restore(m);move(m.widget,paper);rect(m.widget,m.rect);}
    moved.clear();rect(paper,paper_rect);
    // Run the publisher's hide handler before restoring flags. It resets zoom
    // and marks the next stock ItemPreview to reload the player's equipment.
    if(hide)flag(paper,1,false);
    field<uint64_t>(paper,0x30)=hide?(paper_flags&~uint64_t(1)):paper_flags;
    field<int>(paper,0x574)=1;
    paper=nullptr;host=nullptr;modal_hidden=false;changing=false;
}
bool attach(const Rect& css){
    Ptr current=find_host();auto g=game();
    if(!current || !g)return fail("Wardrobe: visible host or browser not found");
    if(GetCurrentThreadId()!=ui_thread)return fail("Wardrobe: callback is not on the native UI thread");
    Ptr browser=lookup(current,"PrivateWardrobeBrowser",0x2012);
    Ptr model_dialog=*reinterpret_cast<Ptr*>(g+0x13875c0+0x110*8);
    if(!browser || !model_dialog || field<uint32_t>(model_dialog,0x340)!=0x110)return fail("Wardrobe: native preview controller not found");
    if(paper && (host!=current || paper!=model_dialog))detach();
    if(!paper){
        // The native controller owns these views for the whole UI lifetime.
        if(!field<Ptr>(model_dialog,0x528) || !field<Ptr>(model_dialog,0x530) || !field<Ptr>(model_dialog,0x538))return fail("Wardrobe: native model views are not initialized");
        if(lookup(model_dialog,"char_model",0x2000)!=field<Ptr>(model_dialog,0x528)
            || lookup(model_dialog,"char_model_zoomin",0x2000)!=field<Ptr>(model_dialog,0x530)
            || lookup(model_dialog,"robot_model",0x2000)!=field<Ptr>(model_dialog,0x538))return fail("Wardrobe: native model lookup does not match controller");
        host=current;paper=model_dialog;paper_rect=rect(paper);paper_flags=field<uint64_t>(paper,0x30);
        for(size_t offset:{size_t(0x528),size_t(0x530),size_t(0x538)}){
            Ptr widget=field<Ptr>(paper,offset);moved.push_back({widget,rect(widget),field<std::array<float,3>>(widget,0x6e8),field<std::array<float,3>>(widget,0x654)});move(widget,host);
        }
        // Keep its controller updating while its window and controls are off-screen.
        // OnVisible restores the stock dialog position, so move it offscreen
        // after that handler has run. Only the reparented views remain onscreen.
        flag(paper,1,true);rect(paper,{-10000,-10000,1,1});log("Wardrobe: native character views attached");
    }
    const Rect browser_rect=rect(browser);
    if(css.x+css.w>browser_rect.w+1 || css.y+css.h>browser_rect.h+1)return fail("Wardrobe: preview bounds exceed native browser");
    Rect r{browser_rect.x+css.x,browser_rect.y+css.y,css.w,css.h};
    for(const auto& m:moved)rect(m.widget,r);
    last_failure=nullptr;return true;
}
std::string string(Ptr value){
    std::array<char,1024> buffer{};size_t n=value?to_utf8(value,buffer.data(),buffer.size()):0;
    if(n>=buffer.size())return {};return std::string(buffer.data(),n);
}
bool local_view(Ptr view){
    Ptr url=view_url(view);std::string s=string(url);if(url)destroy_string(url);
    const std::string expected="http://127.0.0.1:8091/market/wardrobe";
    return s==expected || s.rfind(expected+"?",0)==0;
}
bool journey_view(Ptr view){
    Ptr url=view_url(view);std::string s=string(url);if(url)destroy_string(url);
    const std::string expected="http://127.0.0.1:8091/journey";
    return s==expected || s.rfind(expected+"?",0)==0;
}
Ptr journey_dialog(){
    auto g=game();if(!g)return nullptr;
    for(int id=0x20e;id<=0x221;++id){
        Ptr p=*reinterpret_cast<Ptr*>(g+0x13875c0+id*8);if(!p)continue;
        const char* n=method<const char*(__cdecl*)(Ptr)>(p,0xa8)(p);
        if(n && !std::strcmp(n,"PrivateJourney") && lookup(p,"PrivateJourneyBrowser",0x2012))return p;
    }
    return nullptr;
}
void journey_layout(Ptr dialog){
    auto g=game();Ptr browser=lookup(dialog,"PrivateJourneyBrowser",0x2012);
    const double width=*reinterpret_cast<double*>(g+0x1378ea8),height=*reinterpret_cast<double*>(g+0x1378eb0);
    if(!browser || !std::isfinite(width) || !std::isfinite(height) || width<100 || height<100 || width>16384 || height>16384)return;
    const Rect r{0,0,width,height};const Rect a=rect(dialog),b=rect(browser);
    if(a.x!=r.x || a.y!=r.y || a.w!=r.w || a.h!=r.h)rect(dialog,r);
    if(b.x!=r.x || b.y!=r.y || b.w!=r.w || b.h!=r.h)rect(browser,r);
}
bool parse_preview(Ptr args,Command& command){
    if(array_size(args)!=6)return false;
    command.request=value_integer(array_element(args,5));
    if(command.request<=0)return false;
    Ptr text=value_string(array_element(args,0));std::string ids=string(text);if(text)destroy_string(text);
    Rect r{double(value_integer(array_element(args,1))),double(value_integer(array_element(args,2))),
        double(value_integer(array_element(args,3))),double(value_integer(array_element(args,4)))};
    if(r.x<0 || r.y<0 || r.w<80 || r.h<80 || r.x+r.w>16384 || r.y+r.h>16384)return false;
    std::vector<uint32_t> items;
    if(!ids.empty())for(size_t start=0;start<ids.size();){
        size_t end=ids.find(',',start);if(end==std::string::npos)end=ids.size();
        std::string part=ids.substr(start,end-start);if(part.size()!=9)return false;
        uint32_t item=0;for(char c:part){if(c<'0'||c>'9')return false;item=item*10+c-'0';}
        if(item<100000000 || item>=200000000 || items.size()>=32)return false;
        items.push_back(item);start=end+1;
    }
    command.bounds=r;command.items=std::move(items);return true;
}
void control(const std::string& command,int pressed);
bool preview(const Command& command){
    if(!attach(command.bounds))return false;
    const bool keep_hidden=modal_hidden;
    if(keep_hidden)control("visible",1);
    auto g=game();
    reinterpret_cast<void(__cdecl*)(Ptr,bool)>(g+0x8053f0)(paper,false);
    for(uint32_t item:command.items){uint32_t data[4]={item,0,0,0};reinterpret_cast<void(__cdecl*)(Ptr,const uint32_t*)>(g+0x804bd0)(paper,data);}
    // The stock dialog's next update rebuilds the model. Complete that update
    // before taking the camera baseline; unopened native views have zero cameras.
    field<int>(paper,0x598)=1;
    mouse::refresh_pending=true;
    if(keep_hidden)control("visible",0);
    // Native controls and model flags, including zoom and robot state, stay in the controller.
    return true;
}
void preview_state(Ptr view,bool ready,int request){
    std::wstring code=L"window.WardrobePreviewState&&window.WardrobePreviewState(";
    code+=ready?L"true,":L"false,";code+=std::to_wstring(request)+L")";
    Ptr script=from_wide(code.c_str(),code.size()),frame=from_wide(L"",0);
    if(script&&frame)execute_js(view,script,frame);
    if(script)destroy_string(script);if(frame)destroy_string(frame);
}
void control(const std::string& command,int pressed){
    if(!paper || GetCurrentThreadId()!=ui_thread)return;
    auto g=game();
    if(command=="visible"){
        if(!pressed && !modal_hidden){for(size_t i=0;i<moved.size();++i){modal_flags[i]=field<uint64_t>(moved[i].widget,0x30);flag(moved[i].widget,1,false);}modal_hidden=true;}
        else if(pressed && modal_hidden){for(size_t i=0;i<moved.size();++i)flag(moved[i].widget,1,(modal_flags[i]&1)!=0);modal_hidden=false;}
    }
    else if(command=="left" || command=="right")field<int>(paper,0x570)=pressed?(command=="left"?1:2):0;
    else if(command=="zoom")reinterpret_cast<void(__cdecl*)(Ptr,int)>(g+0x805bd0)(paper,!field<int>(paper,0x57c));
    else if(command=="helmet"){field<int>(paper,0x580)=!field<int>(paper,0x580);reinterpret_cast<void(__cdecl*)(Ptr)>(g+0x805cc0)(paper);}
    else if(command=="combat"){field<int>(paper,0x578)=!field<int>(paper,0x578);field<int>(paper,0x598)=1;reinterpret_cast<void(__cdecl*)(Ptr)>(g+0x805d40)(paper);}
    else if(command=="wings"){
        // Stock paper-doll state 2 uses nwing_001 and displays the wing model.
        // Wings need the full-body view rather than the face zoom view.
        if(field<int>(paper,0x57c))reinterpret_cast<void(__cdecl*)(Ptr,int)>(g+0x805bd0)(paper,0);
        field<int>(paper,0x578)=pressed?2:0;field<int>(paper,0x598)=1;
        reinterpret_cast<void(__cdecl*)(Ptr)>(g+0x805d40)(paper);
    }
    else if(command=="camera-reset")mouse::reset();
}
// Called immediately before Game.dll's browser event pump (RVA 0x131ef0),
// whose stock ItemPreview handler consumes browser messages on the game thread.
void tick(){
    if(!ui_thread){ui_thread=GetCurrentThreadId();log("Wardrobe: native event thread ready");}
    if(GetCurrentThreadId()!=ui_thread || changing)return;
    if(Ptr dialog=journey_dialog();dialog && (field<uint64_t>(dialog,0x30)&1))journey_layout(dialog);
    std::vector<Command> pending;bool stock=false;
    {std::lock_guard<std::mutex> lock(queue_mutex);pending.swap(commands);stock=stock_preview_pending;stock_preview_pending=false;}
    if(stock)detach();
    for(const auto& command:pending){
        {std::lock_guard<std::mutex> lock(queue_mutex);if(!command.state->active)continue;}
        bool ready=false;
        try{
            if(command.control=="journey-visible"){
                if(Ptr dialog=journey_dialog()){flag(dialog,1,command.pressed!=0);if(command.pressed)journey_layout(dialog);}
            }
            else if(stock)continue;
            else if(command.request){ready=preview(command);if(ready && !command.state->model_visible)control("visible",0);}
            else{if(command.control=="visible")command.state->model_visible=command.pressed!=0;control(command.control,command.pressed);}
        }
        catch(...){fail("Wardrobe: native preview update failed");}
        if(command.request){std::lock_guard<std::mutex> lock(queue_mutex);command.state->completed=command.request;command.state->ready=ready;}
    }
    mouse::tick();
}
void __cdecl callback(Ptr view,Ptr object,Ptr name,Ptr args){
    std::string method_name=string(name);
    if(string(object)=="AionObject" && method_name=="JourneyVisibility"){
        if(journey_view(view) && array_size(args)==1){
            int visible=value_integer(array_element(args,0));if(visible!=0 && visible!=1)return;
            std::lock_guard<std::mutex> lock(queue_mutex);
            auto& state=states[view];if(!state)state=std::make_shared<State>();
            if(state->active && commands.size()<128){Command command;command.state=state;command.control="journey-visible";command.pressed=visible;commands.push_back(std::move(command));}
        }return;
    }
    if(string(object)=="AionObject" && (method_name=="WardrobePreview" || method_name=="WardrobeControl" || method_name=="WardrobePoll")){
        if(local_view(view)){
            Command command;bool valid=true;int completed=0;bool ready=false;
            if(method_name=="WardrobePreview")valid=parse_preview(args,command);
            else if(method_name=="WardrobeControl"){
                if(array_size(args)!=2)return;
                Ptr value=value_string(array_element(args,0));command.control=string(value);if(value)destroy_string(value);
                if(command.control!="visible" && command.control!="left" && command.control!="right" && command.control!="zoom" && command.control!="helmet" && command.control!="combat" && command.control!="wings" && command.control!="camera-reset")return;
                command.pressed=value_integer(array_element(args,1))!=0;
            }
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                auto& state=states[view];if(!state)state=std::make_shared<State>();
                if(method_name=="WardrobePoll"){completed=state->completed;ready=state->ready;}
                else if(valid){
                    command.state=state;
                    // Coalesce successive resize/appearance updates, keeping
                    // control ordering and the queue bounded during browser stalls.
                    if(command.request && !commands.empty() && commands.back().request && commands.back().state==state)commands.back()=std::move(command);
                    else if(commands.size()<128)commands.push_back(std::move(command));
                    else if(command.request){state->completed=command.request;state->ready=false;}
                }else if(command.request){state->completed=command.request;state->ready=false;}
            }
            // Awesomium is only accessed here, on its callback thread. Native
            // completion never dereferences a browser view that may be destroyed.
            if(completed)preview_state(view,ready,completed);
        }return;
    }
    if(method_name=="ItemPreview" && !local_view(view)){std::lock_guard<std::mutex> lock(queue_mutex);stock_preview_pending=true;}
    auto found=prior_js.find(view);if(found!=prior_js.end() && found->second && found->second!=callback)found->second(view,object,name,args);
}
void register_methods(Ptr view){
    Ptr object=from_wide(L"AionObject",10), preview=from_wide(L"WardrobePreview",15), control=from_wide(L"WardrobeControl",15),poll=from_wide(L"WardrobePoll",12);
    if(object&&preview&&control&&poll){object_callback(view,object,preview);object_callback(view,object,control);object_callback(view,object,poll);}
    Ptr journey=from_wide(L"JourneyVisibility",17);
    if(object&&journey)object_callback(view,object,journey);
    if(journey)destroy_string(journey);
    if(object)destroy_string(object);if(preview)destroy_string(preview);if(control)destroy_string(control);if(poll)destroy_string(poll);
}
void __cdecl set_js(Ptr view,JSCallback prior){
    if(!view){original_js_set(view,prior);return;}
    if(prior!=callback)prior_js[view]=prior;original_js_set(view,callback);
    register_methods(view);
}
void __cdecl set_object(Ptr view,Ptr object,Ptr method_name){
    object_callback(view,object,method_name);
    if(string(object)=="AionObject" && string(method_name)=="ItemPreview")register_methods(view);
}
bool initialize(HMODULE module){
    auto target=symbol<JSSet>(module,"awe_webview_set_callback_js_callback");
    object_callback=symbol<decltype(object_callback)>(module,"awe_webview_set_object_callback");
    view_url=symbol<decltype(view_url)>(module,"awe_webview_get_url");
    array_size=symbol<decltype(array_size)>(module,"awe_jsarray_get_size");array_element=symbol<decltype(array_element)>(module,"awe_jsarray_get_element");
    value_string=symbol<decltype(value_string)>(module,"awe_jsvalue_to_string");value_integer=symbol<decltype(value_integer)>(module,"awe_jsvalue_to_integer");
    execute_js=symbol<decltype(execute_js)>(module,"awe_webview_execute_javascript");
    const uint8_t bytes[]={0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x48,0x8b,0x4c,0x24,0x08};
    const uint8_t object_bytes[]={0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08};
    if(!target||!object_callback||!view_url||!array_size||!array_element||!value_string||!value_integer||!execute_js||std::memcmp(reinterpret_cast<void*>(target),bytes,sizeof(bytes))||std::memcmp(reinterpret_cast<void*>(object_callback),object_bytes,sizeof(object_bytes)))return false;
    original_js_set=reinterpret_cast<JSSet>(trampoline(reinterpret_cast<void*>(target),sizeof(bytes)));
    auto object_target=object_callback;
    object_callback=reinterpret_cast<decltype(object_callback)>(trampoline(reinterpret_cast<void*>(object_target),sizeof(object_bytes)));
    detour(reinterpret_cast<void*>(object_target),reinterpret_cast<void*>(set_object),sizeof(object_bytes));
    detour(reinterpret_cast<void*>(target),reinterpret_cast<void*>(set_js),sizeof(bytes));return true;
}
void destroy(Ptr view){
    prior_js.erase(view);
    std::lock_guard<std::mutex> lock(queue_mutex);
    auto found=states.find(view);if(found!=states.end()){found->second->active=false;states.erase(found);}
}
void visible(Ptr widget,int event){
    if(GetCurrentThreadId()!=ui_thread || changing)return;
    if((widget==host && event<=0) || (widget==paper && event<=0))detach(widget!=paper);
}
}
