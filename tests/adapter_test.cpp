#ifdef NDEBUG
#undef NDEBUG
#endif
// Execute only against mocked Nexus APIs; no game or real input injection.
#include "../src/addon.cpp"
#include <cassert>
#include <iostream>
#include <map>
#include <set>

namespace mock {
AddonAPI_t api{};
Mumble::Data data{};
NexusLinkData_t link{};
std::vector<int> sent, pressed, released;
bool groundPreview=false;
int groundPlacements=0;
std::map<std::string,INPUTBINDS_PROCESS> binds;
std::set<GUI_RENDER> callbacks;
std::map<std::string,std::string> labels;
void localize(const char* id,const char*,const char* value) {labels[id]=value;}
bool available[ActionCount];
void* allocate(size_t n,void*) {return std::malloc(n);}
void deallocate(void* p,void*) {std::free(p);}
const char* path(const char*) {return "C:\\mounts-adapter-test";}
void log(ELogLevel,const char*,const char* s) {std::cout<<s<<'\n';}
void* datalink(const char* id) {return std::strcmp(id,DL_MUMBLE_LINK)==0?static_cast<void*>(&data):static_cast<void*>(&link);}
bool bound(EGameBinds b) {for (int a=0;a<ActionCount;++a) if (gameBinds[a]==b) return available[a];return false;}
void invoke(EGameBinds b,int32_t) {sent.push_back(b);}
void press(EGameBinds b) {pressed.push_back(b);if (b==GB_MasteryAccess02 || b==GB_MasteryAccess06) groundPreview=true;}
void release(EGameBinds b) {
    released.push_back(b);
    if ((b==GB_MasteryAccess02 || b==GB_MasteryAccess06) && groundPreview) {++groundPlacements;groundPreview=false;}
}
LRESULT sendGame(HWND,UINT message,WPARAM key,LPARAM) {
    if (message==WM_KEYDOWN && key==VK_ESCAPE) groundPreview=false;
    return 0;
}
void reg(const char* id,INPUTBINDS_PROCESS cb,const char*) {binds[id]=cb;}
void dereg(const char* id) {binds.erase(id);}
void addRender(ERenderType,GUI_RENDER cb) {callbacks.insert(cb);}
void remRender(GUI_RENDER cb) {callbacks.erase(cb);}
void window(WNDPROC_CALLBACK) {}
void escape(const char*,bool*) {}
void removeEscape(const char*) {}
void quick(const char*,const char*,const char*,const char*,const char*) {}
void removeQuick(const char*) {}
std::set<std::string> drawnIcons;
Texture_t* texture(const char* id) {drawnIcons.insert(id);return nullptr;}
Texture_t* file(const char*,const char*) {return nullptr;}
void frame(ImVec2 mouse=ImVec2(400,300)) {
    data.UITick++;
    auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(1200,800);io.DeltaTime=1.f/60;io.MousePos=mouse;
    ImGui::NewFrame();render();ImGui::Render();
}
void bind(const std::string& id,bool release=false) {assert(binds.count(id));binds[id](id.c_str(),release);}
void setup() {
    std::fill(std::begin(available),std::end(available),true);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    api.ImguiContext=ImGui::GetCurrentContext();
    api.ImguiMalloc=reinterpret_cast<void*>(allocate);api.ImguiFree=reinterpret_cast<void*>(deallocate);
    api.Paths_GetAddonDirectory=path;api.Log=log;api.DataLink_Get=datalink;
    api.GameBinds_IsBound=bound;api.GameBinds_InvokeAsync=invoke;api.GameBinds_Press=press;api.GameBinds_Release=release;
    api.Localization_Set=localize;
    api.InputBinds_RegisterWithString=reg;api.InputBinds_Deregister=dereg;
    api.GUI_Register=addRender;api.GUI_Deregister=remRender;
    api.WndProc_Register=window;api.WndProc_Deregister=window;
    api.WndProc_SendToGameOnly=sendGame;
    api.GUI_RegisterCloseOnEscape=escape;api.GUI_DeregisterCloseOnEscape=removeEscape;
    api.Textures_Get=texture;api.Textures_GetOrCreateFromFile=file;
    api.QuickAccess_Add=quick;api.QuickAccess_Remove=removeQuick;
    data.UIVersion=2;data.UITick=1;data.Context.MapID=100;data.Context.MapType=Mumble::EMapType::Public;
    data.Context.IsGameFocused=1;data.AvatarPosition.Y=10;
    std::wcscpy(data.Identity,L"{\"name\":\"Mock Character\"}");link.IsGameplay=true;
    load(&api);cfg.spawnAtMouse=true;frame();
    wndProc(reinterpret_cast<HWND>(1),WM_MOUSEMOVE,0,0);
}
}

int main(int argc,char** argv) {
    // Guard the Windows ABI byte layout used by Nexus and GW2.
    static_assert(sizeof(wchar_t)==2);
    static_assert(offsetof(Mumble::Data,Context)==1108);
    static_assert(offsetof(Mumble::Context,MountIndex)==84);
    static_assert(sizeof(AddonDefinition_t)==72);
    static_assert(offsetof(AddonAPI_t,GUI_Register)==32);
    std::filesystem::remove_all("C:\\mounts-adapter-test");
    mock::setup();assert(accepting && mock::callbacks.size()==2 && mock::binds.size()==24);
    // A fresh Nexus setup must still show the configured mounts before game binds exist.
    std::fill(std::begin(mock::available),std::end(mock::available),false);
    mock::drawnIcons.clear();mock::frame();
    for (int a=Raptor;a<=Turtle;++a) assert(mock::drawnIcons.count(textureIds[a]));
    cfg.tapMs=0;mock::bind("MM_CONTEXT");mock::frame();
    assert(radial.visible && radial.actions.size()==9);
    mock::sent.clear();mock::bind("MM_CONTEXT",true);mock::frame(ImVec2(700,400));
    assert(mock::sent.empty() && settingsOpen);
    assert(mock::labels["MM_CONTEXT"]=="Mounts & More: Main radial (hold)");
    for (const auto& id:bindIds) assert(!mock::labels[id].empty());
    settingsOpen=false;showBindingsTab=false;
    std::fill(std::begin(mock::available),std::end(mock::available),true);
    for (int a=0;a<ActionCount;++a) {mock::sent.clear();mock::bind("MM_ACTION_"+std::to_string(a));mock::frame();assert(mock::sent.size()==1 && mock::sent[0]==gameBinds[a]);}
    // Chat, map and focus gates prevent all injection.
    mock::sent.clear();mock::data.Context.IsTextboxFocused=1;mock::bind("MM_ACTION_0");mock::frame();assert(mock::sent.empty());
    mock::data.Context.IsTextboxFocused=0;mock::data.Context.IsMapOpen=1;mock::bind("MM_ACTION_0");mock::frame();assert(mock::sent.empty());
    mock::data.Context.IsMapOpen=0;mock::data.Context.IsGameFocused=0;mock::bind("MM_ACTION_0");mock::frame();assert(mock::sent.empty());
    mock::data.Context.IsGameFocused=1;mock::frame();
    // Mounted and WvW single-action contexts.
    mock::data.Context.MountIndex=Mumble::EMountIndex::Griffon;
    mock::bind("MM_CONTEXT");mock::frame();assert(mock::sent.back()==gameBinds[Unmount]);
    mock::data.Context.MountIndex=Mumble::EMountIndex::None;
    mock::data.Context.MapType=Mumble::EMapType::WvW_Lounge;
    mock::bind("MM_CONTEXT");mock::frame();assert(mock::sent.back()==gameBinds[Warclaw]);
    mock::data.Context.MapType=Mumble::EMapType::Public;
    // Ring select on key release. The first of nine sectors is 20 degrees from origin.
    cfg.contexts[6].actions={Raptor,Springer,Skimmer,Jackal};cfg.tapMs=0;
    mock::bind("MM_CONTEXT");mock::frame(ImVec2(400,300));assert(radial.visible);
    mock::bind("MM_CONTEXT",true);mock::frame(ImVec2(500,400));assert(!radial.visible && mock::sent.back()==gameBinds[Raptor]);
    assert(!ImGui::GetIO().MouseDrawCursor);
    // Tap action never opens the wheel or executes a second action on release.
    cfg.tapMs=500;cfg.contexts[6].tapAction=Springer;
    mock::sent.clear();mock::bind("MM_CONTEXT");mock::bind("MM_CONTEXT",true);mock::frame();
    assert(!radial.visible && mock::sent.size()==1 && mock::sent[0]==gameBinds[Springer]);
    // Asynchronous Nexus callbacks can deliver release before press.
    mock::sent.clear();mock::bind("MM_CONTEXT",true);mock::bind("MM_CONTEXT");mock::frame();
    assert(!radial.held && mock::sent.size()==1 && mock::sent[0]==gameBinds[Springer]);
    // Last-wins queue, released once on combat exit.
    cfg.queue=true;mock::data.Context.IsInCombat=1;mock::frame();mock::sent.clear();
    mock::bind("MM_ACTION_0");mock::bind("MM_ACTION_1");mock::frame();assert(mock::sent.empty() && engine.queued==Springer);
    mock::data.Context.IsInCombat=0;mock::frame();assert(mock::sent.size()==1 && mock::sent[0]==gameBinds[Springer]);
    mock::frame();assert(mock::sent.size()==1);
    // Fast targeting holds and then releases only the configured game bind.
    cfg.groundMode=2;mock::bind("MM_ACTION_15");mock::frame();assert(targeting && mock::pressed.back()==gameBinds[Doorway]);
    mock::bind("MM_CANCEL");mock::frame();assert(!targeting && mock::released.back()==gameBinds[Doorway]);
    assert(mock::groundPlacements==0); // cancellation must dismiss the preview before key release
    // Confirmation still places the skill once; cancellation must not disable all releases.
    mock::bind("MM_ACTION_15");mock::frame(ImVec2(1100,700));assert(targeting);
    ImGui::GetIO().WantCaptureMouse=false;onBind("CONFIRM",false);mock::frame(ImVec2(1100,700));
    assert(!targeting && mock::groundPlacements==1);
    // Focus loss also dismisses the preview rather than casting at a stale cursor.
    mock::bind("MM_ACTION_15");mock::frame();assert(targeting);
    mock::data.Context.IsGameFocused=0;mock::frame();assert(!targeting && mock::groundPlacements==1);
    mock::data.Context.IsGameFocused=1;mock::frame();
    // Invalid/malformed configs are preserved and never overwritten.
    save();assert(std::filesystem::exists(configPath));
    {std::ofstream file(configPath);file<<"{bad json";}
    loadSettings();assert(!configWritable);save();
    {std::ifstream file(configPath);std::string s;std::getline(file,s);assert(s=="{bad json");}
    unload();assert(mock::callbacks.empty() && mock::binds.empty() && !accepting);
    std::cout<<"adapter: API6 ABI, lifecycle, all 21 game binds, input gates, radial, tap, queue, ground targeting, config preservation passed\n";
    if (argc>1) {
        HMODULE dll=LoadLibraryA(argv[1]);assert(dll);
        auto get=reinterpret_cast<AddonDefinition_t*(*)()>(GetProcAddress(dll,"GetAddonDef"));assert(get);
        auto* def=get();assert(def && def->APIVersion==6 && def->Version.Major==0 && def->Version.Minor==1);
        def->Load(&mock::api);assert(mock::callbacks.size()==2 && mock::binds.size()==24);
        for (int a=0;a<ActionCount;++a) {
            mock::sent.clear();mock::bind("MM_ACTION_"+std::to_string(a));mock::data.UITick++;
            auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(1200,800);io.DeltaTime=1.f/60;io.MousePos=ImVec2(1100,700);
            ImGui::NewFrame();for (auto cb:mock::callbacks) cb();ImGui::Render();
            assert(mock::sent.size()==1 && mock::sent[0]==gameBinds[a]);
        }
        def->Unload();assert(mock::callbacks.empty() && mock::binds.empty());FreeLibrary(dll);
        std::cout<<"binary: shipped DLL LoadLibrary/GetAddonDef/load/render/21 actions/unload passed\n";
    }
    ImGui::DestroyContext();
}
