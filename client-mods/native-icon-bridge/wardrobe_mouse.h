// Native model views sit above the browser, so DOM mouse listeners cannot
// reliably receive their input. Capture only this preview's window messages;
// all model changes still run in wardrobe::tick, never in the window callback.
namespace mouse {
std::mutex mutex;
HWND window=nullptr;
WNDPROC prior=nullptr;
Rect bounds{};
bool active=false;
int dragging=0;
POINT anchor{};
float rotation=0,pan_x=0,pan_y=0,wheel=0;
float scale=1,offset_x=0,offset_z=0;
bool refresh_pending=false;
ULONGLONG retry_after=0;
float clamp(float n,float lo,float hi){return (std::max)(lo,(std::min)(hi,n));}
bool inside(POINT p){return p.x>=bounds.x && p.y>=bounds.y && p.x<bounds.x+bounds.w && p.y<bounds.y+bounds.h;}
LRESULT CALLBACK receive(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
    POINT point{static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
    if(message==WM_MOUSEWHEEL)ScreenToClient(hwnd,&point);
    bool consume=false,capture=false,release=false;WNDPROC forward;
    {
        std::lock_guard<std::mutex> lock(mutex);forward=prior;
        if(active && (message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN) && inside(point)){
            dragging=message==WM_LBUTTONDOWN?1:2;anchor=point;capture=consume=true;
        }else if(message==WM_MOUSEMOVE && dragging){
            if(active){
                float dx=clamp(float(point.x-anchor.x),-200,200),dy=clamp(float(point.y-anchor.y),-200,200);
                if(dragging==1)rotation=clamp(rotation+dx,-1000,1000);
                else{pan_x=clamp(pan_x+dx,-1000,1000);pan_y=clamp(pan_y+dy,-1000,1000);}
                anchor=point;consume=true;
            }else{dragging=0;release=true;}
        }else if((message==WM_LBUTTONUP && dragging==1) || (message==WM_RBUTTONUP && dragging==2)){
            dragging=0;release=consume=true;
        }else if(active && message==WM_MOUSEWHEEL && inside(point)){
            wheel=clamp(wheel+float(static_cast<short>(HIWORD(wparam)))/WHEEL_DELTA,-20,20);consume=true;
        }else if(message==WM_KILLFOCUS || message==WM_CANCELMODE || message==WM_CAPTURECHANGED){
            dragging=0;
            if(message!=WM_CAPTURECHANGED)rotation=pan_x=pan_y=wheel=0;
            release=message!=WM_CAPTURECHANGED;
        }
    }
    // SetCapture/ReleaseCapture can synchronously dispatch another message.
    if(capture)SetCapture(hwnd);
    if(release && GetCapture()==hwnd)ReleaseCapture();
    LRESULT result=consume?0:(forward?CallWindowProcW(forward,hwnd,message,wparam,lparam):DefWindowProcW(hwnd,message,wparam,lparam));
    if(message==WM_NCDESTROY){std::lock_guard<std::mutex> lock(mutex);if(window==hwnd){window=nullptr;prior=nullptr;active=false;dragging=0;}}
    return result;
}
BOOL CALLBACK find_window(HWND candidate,LPARAM value){
    DWORD pid=0;GetWindowThreadProcessId(candidate,&pid);RECT r{};
    if(pid==GetCurrentProcessId() && IsWindowVisible(candidate) && GetClientRect(candidate,&r) && r.right>=640 && r.bottom>=480){
        *reinterpret_cast<HWND*>(value)=candidate;return FALSE;
    }return TRUE;
}
void install(){
    if(window)return;
    HWND candidate=nullptr;EnumWindows(find_window,reinterpret_cast<LPARAM>(&candidate));if(!candidate)return;
    std::lock_guard<std::mutex> lock(mutex);
    SetLastError(0);auto previous=SetWindowLongPtrW(candidate,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(receive));
    if(previous){window=candidate;prior=reinterpret_cast<WNDPROC>(previous);log("Wardrobe: native preview mouse controls attached");}
}
void stop(){
    HWND captured;
    {std::lock_guard<std::mutex> lock(mutex);active=false;dragging=0;rotation=pan_x=pan_y=wheel=0;captured=window;}
    if(captured && GetCapture()==captured)ReleaseCapture();
    scale=1;offset_x=offset_z=0;refresh_pending=false;retry_after=0;
}
void rebuild_camera(Ptr widget){
    // Position and frustum planes are separate native camera data. Updating
    // position alone leaves the renderer using the previous view/frustum.
    reinterpret_cast<void(__cdecl*)(Ptr,int,int)>(game()+0x1bf0d0)(static_cast<uint8_t*>(widget)+0x6b8,-1,-1);
    field<int>(widget,0x610)=1;
}
bool valid_camera(const std::array<float,3>& camera){
    return std::isfinite(camera[0]) && std::isfinite(camera[1]) && std::isfinite(camera[2]) && camera[1]>0.05f;
}
void restore(const Moved& model){
    const auto& camera=valid_camera(model.camera)?model.camera:model.base_camera;
    if(valid_camera(camera)){
        field<std::array<float,3>>(model.widget,0x6e8)=camera;
        field<std::array<float,3>>(model.widget,0x8bc)=camera;
        rebuild_camera(model.widget);
    }
    field<std::array<float,3>>(model.widget,0x654)=model.angle;
    field<int>(model.widget,0x610)=1;
}
void reset(){
    stop();
    for(auto& model:moved){
        restore(model);model.preview_angle=model.angle;
        if(model.camera_ready)model.base_camera=field<std::array<float,3>>(model.widget,0x6e8);
    }
}
bool prepare_models(){
    if(!paper || modal_hidden || moved.empty())return false;
    const auto now=GetTickCount64();
    const bool reload=field<int>(paper,0x598)!=0 || (refresh_pending && now>=retry_after);
    if(reload){
        // Restore camera offsets before the publisher recomputes its framing.
        // This preserves camera input across Try On and avoids accumulating pan.
        for(auto& model:moved)if(model.camera_ready){
            field<std::array<float,3>>(model.widget,0x6e8)=model.base_camera;
            field<std::array<float,3>>(model.widget,0x8bc)=model.base_camera;
        }
        reinterpret_cast<void(__cdecl*)(Ptr)>(game()+0x8046f0)(paper);
        field<int>(paper,0x598)=0;refresh_pending=false;
    }
    bool changed=reload,attempted=false;
    for(auto& model:moved){
        Ptr widget=model.widget;const auto entity=field<Ptr>(widget,0x470);
        if(!(field<uint64_t>(widget,0x30)&1))continue;
        const auto current=field<std::array<float,3>>(widget,0x6e8);
        const bool invalid=!entity || !valid_camera(current);
        if(!reload && model.camera_ready && model.entity==entity && !invalid){
            // Stock Left/Right controls also change the model angle.
            model.preview_angle=field<std::array<float,3>>(widget,0x654);
            continue;
        }
        if(!reload && invalid && now<retry_after)continue;
        attempted=true;
        if(!entity){model.camera_ready=false;refresh_pending=true;continue;}
        // The same native framing routine used when Zoom loads its new view.
        reinterpret_cast<void(__cdecl*)(Ptr)>(game()+0x48e340)(widget);
        const auto camera=field<std::array<float,3>>(widget,0x6e8);
        if(!valid_camera(camera)){model.camera_ready=false;continue;}
        model.base_camera=camera;model.entity=entity;
        if(!model.camera_ready)model.preview_angle=field<std::array<float,3>>(widget,0x654);
        model.camera_ready=true;
        field<std::array<float,3>>(widget,0x654)=model.preview_angle;
        rebuild_camera(widget);changed=true;
    }
    if(attempted)retry_after=now+250;
    return changed;
}
void apply(float dx,float px,float py,float steps){
    if(!paper || modal_hidden || moved.empty())return;
    static unsigned observed=0;
    if(dx && !(observed&1)){log("Wardrobe: preview drag rotation received");observed|=1;}
    if((px || py) && !(observed&2)){log("Wardrobe: preview drag positioning received");observed|=2;}
    if(steps && !(observed&4)){log("Wardrobe: preview wheel zoom received");observed|=4;}
    scale=clamp(scale*std::pow(0.88f,steps),0.35f,2.5f);
    offset_x=clamp(offset_x-px*0.004f*scale,-2,2);
    offset_z=clamp(offset_z+py*0.004f*scale,-2,2);
    for(auto& model:moved){
        if(!model.camera_ready)continue;
        Ptr widget=model.widget;
        // Same angle function used by the stock Left/Right preview controls.
        if(dx){
            field<std::array<float,3>>(widget,0x654)=model.preview_angle;
            reinterpret_cast<void(__cdecl*)(Ptr,float,float,float)>(game()+0x487480)(widget,1,1,dx*0.45f);
            model.preview_angle=field<std::array<float,3>>(widget,0x654);
        }
        auto camera=model.base_camera;camera[0]+=offset_x;camera[1]*=scale;camera[2]+=offset_z;
        if(field<std::array<float,3>>(widget,0x6e8)!=camera || dx){
            field<std::array<float,3>>(widget,0x6e8)=camera;
            field<std::array<float,3>>(widget,0x8bc)=camera;
            rebuild_camera(widget);
        }
    }
}
void tick(){
    if(!paper || !host)return;
    const bool refreshed=prepare_models();
    install();Rect hit{};bool enabled=false;
    if(!modal_hidden && (field<uint64_t>(host,0x30)&1))for(const auto& model:moved){
        if(field<uint64_t>(model.widget,0x30)&1){method<void(__cdecl*)(Ptr,Rect*)>(model.widget,0x58)(model.widget,&hit);enabled=true;break;}
    }
    float dx,px,py,steps;
    {
        std::lock_guard<std::mutex> lock(mutex);bounds=hit;active=enabled && window;
        dx=rotation;px=pan_x;py=pan_y;steps=wheel;rotation=pan_x=pan_y=wheel=0;
    }
    if(refreshed || dx || px || py || steps || scale!=1 || offset_x || offset_z)apply(dx,px,py,steps);
}
}
