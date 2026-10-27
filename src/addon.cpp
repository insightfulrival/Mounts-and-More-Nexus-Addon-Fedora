// Nexus adapter. Game state, settings and all ImGui calls belong to the render thread.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include "Nexus.h"
#include "Mumble.h"
#include "imgui.h"
#include "json.hpp"
#include "core.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
using namespace mounts;
namespace {
constexpr uint32_t Signature = static_cast<uint32_t>(-1296911960); // local development ID, not a Raidcore allocation
constexpr float Pi = 3.14159265358979323846f;
AddonAPI_t* api = nullptr;
Mumble::Data* mumble = nullptr;
NexusLinkData_t* nexus = nullptr;
std::string directory, configPath, status;
std::atomic<bool> accepting{false}, targeting{false}, captureRadial{false};
std::atomic<bool> consumeConfirmUp{false};
std::atomic<int> jumpVirtualKey{VK_SPACE};
std::atomic<bool> masteryBusy{false};
std::atomic<int> masteryResult{-2}; // -2 not checked; -1 failure; 0 locked; 1 unlocked
std::thread masteryWorker;
std::mutex eventMutex;
struct Event { std::string id; bool release; double time; };
std::deque<Event> events;
std::map<std::string,double> earlyReleases;
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::vector<std::string> bindIds;
std::array<std::string, ActionCount> iconFiles;
std::array<std::string, ActionCount> textureIds;
std::array<Texture_t*, ActionCount> textures{};
std::array<uint64_t, ActionCount> used{};
uint64_t serial = 0;
Snapshot state, previous;
FallingDetector falling;
Engine engine;
bool settingsOpen = false, firstSample = true, changed = false, configWritable = true;
bool infoPositionRequested = true;
bool cursorRestored = false;
POINT originalCursor{};
uint32_t lastTick = 0;
double lastTickAt = 0;
int targetAction = -1;
bool targetPressed = false, restoreActionCam = false;
std::string targetCharacter;
uint32_t targetMap = 0;
char apiToken[256]{};

struct Bar {
    std::string name = "Mounts";
    bool enabled = true, vertical = false, competitive = true, locked = false;
    float x = 100, y = 100, size = 44, opacity = 1;
    std::vector<int> actions{Raptor, Springer, Skimmer, Jackal, Griffon, Roller, Warclaw, Skyscale, Turtle};
};
struct Settings {
    bool queue = false, autoRemount = false, combatLaunch = false, showLoading = false;
    bool spawnAtMouse = false, defaultOnly = false, info = true, quickAccess = false;
    bool showQueue = true, showLater = true, showGround = true, infoLocked = false;
    float infoX = 200, infoY = 200;
    float radius = 170, iconSize = 56, opacity = 0.7f, startAngle = 0, frequency = 0.1f;
    int tapMs = 500, groundMode = 0, jumpKey = VK_SPACE; // normal, instant, fast with range indicator
    std::array<RadialProfile, 7> contexts;
    std::vector<RadialProfile> custom;
    std::vector<Bar> bars{Bar{}};
} cfg;
struct ActiveRadial {
    bool held = false, visible = false, waiting = false;
    std::string bind;
    double pressedAt = 0;
    RadialProfile profile;
    ImVec2 origin;
    int center = -1;
    std::vector<int> actions;
} radial;

const char* name(int a) { return validAction(a) ? actionNames[a] : "None"; }
EGameBinds gameBind(int a) { return static_cast<EGameBinds>(gameBinds[a]); }
bool bound(int a) { return validAction(a) && api->GameBinds_IsBound(gameBind(a)); }
bool inputAllowed() { return state.valid && state.focused && !state.textbox && !state.mapOpen; }
bool canAct() { return inputAllowed() && state.gameplay; }
EngineOptions options() {
    EngineOptions o;
    o.queueEnabled = cfg.queue;
    o.autoRemount = cfg.autoRemount;
    o.combatLaunch = cfg.combatLaunch && masteryResult.load() != 0;
    return o;
}
void log(ELogLevel level, const std::string& message) { api->Log(level, "Mounts & More", message.c_str()); }

std::vector<int> actionList(const json& j) {
    std::vector<int> result;
    if (!j.is_array() || j.size() > ActionCount) throw std::runtime_error("Invalid action list");
    for (const auto& a : j) {
        if (!a.is_number_integer() || !validAction(a.get<int>())) throw std::runtime_error("Invalid action ID");
        int v = a.get<int>();
        if (std::find(result.begin(), result.end(), v) == result.end()) result.push_back(v);
    }
    return result;
}
json profileJson(const RadialProfile& p) {
    return {{"name",p.name},{"enabled",p.enabled},{"actions",p.actions},{"center",p.centerBehavior},
        {"default",p.defaultAction},{"removeCenter",p.removeCenter},{"single",p.instantSingle},
        {"tap",p.tapAction},{"unconditional",p.unconditional}};
}
RadialProfile readProfile(const json& j, RadialProfile p = {}) {
    p.name = j.value("name",p.name).substr(0,100);
    p.enabled = j.value("enabled",p.enabled);
    p.actions = actionList(j.at("actions"));
    p.centerBehavior = std::clamp(j.value("center",0),0,2);
    p.defaultAction = j.value("default",-1); if (!validAction(p.defaultAction)) p.defaultAction = -1;
    p.removeCenter = j.value("removeCenter",true);
    p.instantSingle = j.value("single",false);
    p.tapAction = j.value("tap",-1); if (!validAction(p.tapAction)) p.tapAction = -1;
    p.unconditional = j.value("unconditional",false);
    return p;
}
void defaults() {
    for (int i=0;i<7;++i) { cfg.contexts[i].name=contextNames[i]; cfg.contexts[i].enabled=false; }
    auto& mounted=cfg.contexts[0]; mounted.enabled=true; mounted.instantSingle=true;
    mounted.unconditional=true; mounted.actions={Unmount};
    auto& wvw=cfg.contexts[1]; wvw.enabled=true; wvw.instantSingle=true; wvw.actions={Warclaw};
    cfg.contexts[2].instantSingle=true; cfg.contexts[2].actions={Skyscale};
    cfg.contexts[3].actions={Skimmer,Turtle};
    cfg.contexts[4].instantSingle=true; cfg.contexts[4].actions={Skiff};
    cfg.contexts[5].actions={Griffon,Skyscale};
    cfg.contexts[6].enabled=true; cfg.contexts[6].actions=cfg.bars[0].actions;
    for (int i=0;i<ActionCount;++i) iconFiles[i]=std::string(iconStems[i])+".png";
}
void save() {
    if (!configWritable) { status="Settings file is unreadable. Rename it before saving; original preserved."; return; }
    try {
        json j={{"schema",1},{"queue",cfg.queue},{"autoRemount",cfg.autoRemount},{"combatLaunch",cfg.combatLaunch},
            {"showLoading",cfg.showLoading},{"spawnAtMouse",cfg.spawnAtMouse},{"defaultOnly",cfg.defaultOnly},
            {"info",cfg.info},{"quickAccess",cfg.quickAccess},{"radius",cfg.radius},{"iconSize",cfg.iconSize},
            {"showQueue",cfg.showQueue},{"showLater",cfg.showLater},{"showGround",cfg.showGround},
            {"infoLocked",cfg.infoLocked},{"infoX",cfg.infoX},{"infoY",cfg.infoY},
            {"opacity",cfg.opacity},{"startAngle",cfg.startAngle},{"frequency",cfg.frequency},
            {"tapMs",cfg.tapMs},{"groundMode",cfg.groundMode},{"jumpKey",cfg.jumpKey},{"icons",iconFiles}};
        j["contexts"]=json::array(); for (const auto& p:cfg.contexts) j["contexts"].push_back(profileJson(p));
        j["custom"]=json::array(); for (const auto& p:cfg.custom) j["custom"].push_back(profileJson(p));
        j["bars"]=json::array(); for (const auto& b:cfg.bars) j["bars"].push_back({{"name",b.name},
            {"enabled",b.enabled},{"vertical",b.vertical},{"competitive",b.competitive},{"locked",b.locked},
            {"x",b.x},{"y",b.y},{"size",b.size},{"opacity",b.opacity},{"actions",b.actions}});
        const auto temporary=configPath+".tmp";
        { std::ofstream f(temporary,std::ios::binary|std::ios::trunc); f.exceptions(std::ios::badbit|std::ios::failbit); f<<j.dump(2)<<'\n'; f.close(); }
        if (!MoveFileExA(temporary.c_str(),configPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace settings");
        status="Settings saved."; changed=false;
    } catch (const std::exception& e) { status=std::string("Save failed: ")+e.what(); log(LOGL_WARNING,status); }
}
void loadSettings() {
    defaults();
    if (!std::filesystem::exists(configPath)) return;
    try {
        if (std::filesystem::file_size(configPath)>1024*1024) throw std::runtime_error("Settings exceed 1 MB");
        std::ifstream f(configPath); json j; f>>j;
        if (j.at("schema")!=1) throw std::runtime_error("Unsupported settings schema");
        cfg.queue=j.value("queue",false); cfg.autoRemount=j.value("autoRemount",false);
        cfg.combatLaunch=j.value("combatLaunch",false); cfg.showLoading=j.value("showLoading",false);
        cfg.spawnAtMouse=j.value("spawnAtMouse",false); cfg.defaultOnly=j.value("defaultOnly",false);
        cfg.info=j.value("info",true); cfg.quickAccess=j.value("quickAccess",false);
        cfg.showQueue=j.value("showQueue",true);cfg.showLater=j.value("showLater",true);
        cfg.showGround=j.value("showGround",true);cfg.infoLocked=j.value("infoLocked",false);
        cfg.infoX=std::clamp(j.value("infoX",200.f),0.f,16000.f);
        cfg.infoY=std::clamp(j.value("infoY",200.f),0.f,16000.f);infoPositionRequested=true;
        cfg.radius=std::clamp(j.value("radius",170.f),60.f,450.f);
        cfg.iconSize=std::clamp(j.value("iconSize",56.f),20.f,120.f);
        cfg.opacity=std::clamp(j.value("opacity",0.7f),0.1f,1.f);
        cfg.startAngle=std::clamp(j.value("startAngle",0.f),-360.f,360.f);
        cfg.frequency=std::clamp(j.value("frequency",0.1f),0.01f,1.f);
        cfg.tapMs=std::clamp(j.value("tapMs",500),0,2000);
        cfg.groundMode=std::clamp(j.value("groundMode",0),0,2);
        cfg.jumpKey=std::clamp(j.value("jumpKey",VK_SPACE),1,255); jumpVirtualKey=cfg.jumpKey;
        if (j.at("contexts").size()!=7) throw std::runtime_error("Expected seven contexts");
        for (int i=0;i<7;++i) cfg.contexts[i]=readProfile(j["contexts"][i],cfg.contexts[i]);
        if (j.at("custom").size()>32 || j.at("bars").size()>32) throw std::runtime_error("Too many profiles");
        cfg.custom.clear(); for (const auto& p:j["custom"]) cfg.custom.push_back(readProfile(p));
        cfg.bars.clear(); for (const auto& v:j["bars"]) {
            Bar b; b.name=v.value("name",b.name).substr(0,100); b.enabled=v.value("enabled",true);
            b.vertical=v.value("vertical",false); b.competitive=v.value("competitive",true); b.locked=v.value("locked",false);
            b.x=std::clamp(v.value("x",100.f),0.f,16000.f); b.y=std::clamp(v.value("y",100.f),0.f,16000.f);
            b.size=std::clamp(v.value("size",44.f),20.f,120.f); b.opacity=std::clamp(v.value("opacity",1.f),0.1f,1.f);
            b.actions=actionList(v.at("actions")); cfg.bars.push_back(b);
        }
        if (j.contains("icons") && j["icons"].size()==ActionCount) for (int i=0;i<ActionCount;++i) {
            auto file=j["icons"][i].get<std::string>();
            if (file.find_first_of("/\\:")==std::string::npos && file.size()<160 && std::filesystem::path(file).extension()==".png") iconFiles[i]=file;
        }
    } catch (const std::exception& e) {
        cfg=Settings{}; defaults(); configWritable=false;
        status=std::string("Settings not loaded; original preserved: ")+e.what(); log(LOGL_WARNING,status);
    }
}

void onBind(const char* id,bool release) {
    std::lock_guard<std::mutex> guard(eventMutex);
    if (accepting && events.size()<256) events.push_back({id,release,now()});
}
void registerBinds() {
    for (const auto& id:bindIds) api->InputBinds_Deregister(id.c_str());
    bindIds.clear();
    auto add=[](std::string id,const char* key) { bindIds.push_back(id); api->InputBinds_RegisterWithString(id.c_str(),onBind,key); };
    add("MM_CONTEXT","CTRL+ALT+M"); add("MM_SETTINGS","CTRL+SHIFT+M");
    add("MM_CANCEL","CTRL+ALT+BACKSPACE");
    for (int i=0;i<ActionCount;++i) add("MM_ACTION_"+std::to_string(i),"(null)");
    for (size_t i=0;i<cfg.custom.size();++i) if (!cfg.custom[i].name.empty()) add("MM_RADIAL_"+std::to_string(i),"(null)");
}
void refreshTextures() {
    static unsigned generation=0; ++generation;
    for (int i=0;i<ActionCount;++i) {
        textureIds[i]="MM_ICON_"+std::to_string(generation)+"_"+std::to_string(i);
        auto file=directory+"\\icons\\"+iconFiles[i];
        textures[i]=api->Textures_GetOrCreateFromFile(textureIds[i].c_str(),file.c_str());
    }
}
void removeQuickAccess() { for (int i=0;i<ActionCount;++i) api->QuickAccess_Remove(("MM_QA_"+std::to_string(i)).c_str()); }
void updateQuickAccess() {
    removeQuickAccess();
    if (!cfg.quickAccess) return;
    for (int i=0;i<ActionCount;++i) if (bound(i)) api->QuickAccess_Add(("MM_QA_"+std::to_string(i)).c_str(),
        textureIds[i].c_str(),textureIds[i].c_str(),("MM_ACTION_"+std::to_string(i)).c_str(),name(i));
}

Snapshot readState(double t) {
    Snapshot s;
    if (!mumble) mumble=static_cast<Mumble::Data*>(api->DataLink_Get(DL_MUMBLE_LINK));
    if (!nexus) nexus=static_cast<NexusLinkData_t*>(api->DataLink_Get(DL_NEXUS_LINK));
    if (!mumble || !nexus || mumble->UIVersion!=2) return s;
    if (mumble->UITick!=lastTick) { lastTick=mumble->UITick; lastTickAt=t; }
    s.valid=lastTick!=0 && t-lastTickAt<2.0;
    s.gameplay=nexus->IsGameplay;
    const auto& c=mumble->Context;
    s.focused=c.IsGameFocused; s.mapOpen=c.IsMapOpen; s.textbox=c.IsTextboxFocused;
    s.combat=c.IsInCombat; s.competitive=c.IsCompetitive;
    s.mapId=c.MapID; s.mapType=static_cast<uint32_t>(c.MapType); s.mount=static_cast<int>(c.MountIndex);
    // Gw2Sharp/Blish uses (Mumble.X, Mumble.Z, Mumble.Y), in meters.
    s.x=mumble->AvatarPosition.X; s.y=mumble->AvatarPosition.Z; s.z=mumble->AvatarPosition.Y;
    // Raw identity avoids the SDK's 20-byte truncated display-name field.
    int length=0; while (length<256 && mumble->Identity[length]) ++length;
    char buffer[2048]{};
    int bytes=WideCharToMultiByte(CP_UTF8,0,mumble->Identity,length,buffer,sizeof(buffer),nullptr,nullptr);
    if (bytes>0) {
        auto identity=json::parse(buffer,buffer+bytes,nullptr,false);
        if (identity.is_object() && identity.contains("name") && identity["name"].is_string()) s.character=identity["name"].get<std::string>();
    }
    if (firstSample || s.mapId!=previous.mapId || s.character!=previous.character || !s.gameplay || !s.valid) falling.reset(s.z,t);
    s.falling=falling.sample(s.z,t,cfg.frequency); firstSample=false;
    return s;
}

void releaseTarget() {
    if (targetPressed && validAction(targetAction)) {
        // Fast targeting casts on key-up. Cancel its preview first. Nexus posts both
        // passthrough messages and the synchronous GameBinds release to the same game
        // message queue in this order; no system-wide input is generated here.
        const LPARAM down=1|(static_cast<LPARAM>(MapVirtualKeyA(VK_ESCAPE,MAPVK_VK_TO_VSC))<<16);
        api->WndProc_SendToGameOnly(nullptr,WM_KEYDOWN,VK_ESCAPE,down);
        api->WndProc_SendToGameOnly(nullptr,WM_KEYUP,VK_ESCAPE,down|(LPARAM(1)<<30)|(LPARAM(1)<<31));
        api->GameBinds_Release(gameBind(targetAction));
    }
    targetPressed=false; targetAction=-1; targeting=false; targetCharacter.clear();
}
void send(int action) {
    if (!bound(action) || !canAct()) return;
    api->GameBinds_InvokeAsync(gameBind(action),50);
    used[action]=++serial;
}
void execute(int action,bool fromUI=false,bool unconditional=false) {
    if (!bound(action) || !inputAllowed()) return;
    int ready=engine.request(action,state,options(),unconditional);
    if (!validAction(ready)) return;
    if (fromUI && isGroundTargeted(ready) && cfg.groundMode!=0) {
        if (cfg.groundMode==2 || delayedGroundTarget(ready,state)) {
            releaseTarget(); targetAction=ready; targetCharacter=state.character; targetMap=state.mapId; targeting=true;
            if (cfg.groundMode==2) { api->GameBinds_Press(gameBind(ready)); targetPressed=true; }
            status="Ground target ready: click in the game to confirm, Escape to cancel.";
            return;
        }
    }
    send(ready);
}
void restoreCamera() {
    if (restoreActionCam && canAct()) { api->GameBinds_InvokeAsync(GB_CameraActionMode,50); restoreActionCam=false; }
}
void closeRadial(bool restoreCursor=true) {
    radial=ActiveRadial{}; captureRadial=false;
    restoreCamera();
    if (restoreCursor && cursorRestored) { SetCursorPos(originalCursor.x,originalCursor.y); cursorRestored=false; }
}
void cancelAll() { releaseTarget(); closeRadial(); engine.cancel(); earlyReleases.clear(); }
int centerAction(const RadialProfile& p) {
    if (p.centerBehavior==1 && bound(p.defaultAction) && std::find(p.actions.begin(),p.actions.end(),p.defaultAction)!=p.actions.end()) return p.defaultAction;
    int result=-1;
    if (p.centerBehavior==2) for (int a:p.actions) if (bound(a) && used[a] && (result<0 || used[a]>used[result])) result=a;
    return result;
}
int selectedAction() {
    if (!radial.visible) return -1;
    auto mouse=ImGui::GetIO().MousePos;
    int sector=chooseSector(mouse.x-radial.origin.x,mouse.y-radial.origin.y,
        static_cast<int>(radial.actions.size()),cfg.startAngle,cfg.iconSize*0.70710678f);
    return sector>=0 ? radial.actions[sector] : radial.center;
}
void showRadial() {
    radial.waiting=false;
    auto& p=radial.profile;
    radial.actions.clear();
    for (int a:p.actions) if (bound(a) && !(cfg.tapMs>0 && a==p.tapAction)) radial.actions.push_back(a);
    if (p.instantSingle && radial.actions.size()==1) { execute(radial.actions.front(),false,p.unconditional); radial.held=false; return; }
    if (cfg.defaultOnly) { execute(p.defaultAction,false,p.unconditional); radial.held=false; return; }
    if (nexus && nexus->IsCameraMoving && bound(p.defaultAction)) { execute(p.defaultAction); radial.held=false; return; }
    radial.center=centerAction(p);
    if (p.removeCenter && radial.center>=0) radial.actions.erase(std::remove(radial.actions.begin(),radial.actions.end(),radial.center),radial.actions.end());
    auto& io=ImGui::GetIO();
    radial.origin=cfg.spawnAtMouse ? io.MousePos : ImVec2(io.DisplaySize.x*0.5f,io.DisplaySize.y*0.5f);
    // Cursor visibility is a Win32 fact within the same Wine process, not a Wayland global-input query.
    CURSORINFO cursor{sizeof(CURSORINFO)};
    if (GetCursorInfo(&cursor) && !(cursor.flags&CURSOR_SHOWING) && api->GameBinds_IsBound(GB_CameraActionMode)) {
        api->GameBinds_InvokeAsync(GB_CameraActionMode,50); restoreActionCam=true;
    }
    if (!cfg.spawnAtMouse && GetCursorPos(&originalCursor)) {
        HWND window=GetForegroundWindow(); POINT pnt{static_cast<LONG>(radial.origin.x),static_cast<LONG>(radial.origin.y)};
        ClientToScreen(window,&pnt); SetCursorPos(pnt.x,pnt.y); cursorRestored=true;
        io.MousePos=radial.origin;
    }
    radial.visible=true; captureRadial=true;
}
void beginRadial(const std::string& id,const RadialProfile& p,double time,bool contextual) {
    if (!inputAllowed() || (!state.gameplay && !cfg.showLoading) || !p.enabled) return;
    closeRadial(); releaseTarget();
    radial.held=true; radial.bind=id; radial.profile=p; radial.pressedAt=time;
    radial.waiting=contextual && cfg.tapMs>0 && bound(p.tapAction);
    if (!radial.waiting) showRadial();
}
void processEvents(double t) {
    std::deque<Event> pending;
    { std::lock_guard<std::mutex> guard(eventMutex); pending.swap(events); }
    for (auto i=earlyReleases.begin();i!=earlyReleases.end();) {
        if (t-i->second>0.15) i=earlyReleases.erase(i); else ++i;
    }
    while (!pending.empty()) {
        auto e=std::move(pending.front());pending.pop_front();
        if (e.id=="MM_SETTINGS") { if (!e.release) settingsOpen=!settingsOpen; continue; }
        if (e.id=="MM_CANCEL" || e.id=="ESCAPE") { if (!e.release) cancelAll(); continue; }
        if (e.id=="JUMP") { if (!e.release) falling.jump(e.time,state.mount!=0); continue; }
        if (e.id=="CONFIRM") {
            if (!e.release && targeting && canAct() && !ImGui::GetIO().WantCaptureMouse && state.character==targetCharacter && state.mapId==targetMap) {
                int a=targetAction;
                if (targetPressed) { api->GameBinds_Release(gameBind(a)); used[a]=++serial; }
                else send(a);
                targetPressed=false; targetAction=-1; targeting=false;
            }
            continue;
        }
        if (e.release) {
            if (!radial.held || radial.bind!=e.id) {
                if (e.id=="MM_CONTEXT" || e.id.rfind("MM_RADIAL_",0)==0) earlyReleases[e.id]=e.time;
                continue;
            }
            const auto p=radial.profile;
            if (radial.waiting && (e.time-radial.pressedAt)*1000.0<=cfg.tapMs) {
                closeRadial(); execute(p.tapAction,false,p.unconditional);
            } else {
                if (radial.waiting) showRadial();
                int a=selectedAction(); closeRadial(); execute(a,true,p.unconditional);
            }
            continue;
        }
        if (!inputAllowed()) continue;
        if (e.id=="MM_CONTEXT") {
            for (int i=0;i<7;++i) if (cfg.contexts[i].enabled && contextApplicable(i,state)) { beginRadial(e.id,cfg.contexts[i],e.time,true); break; }
        } else if (e.id.rfind("MM_RADIAL_",0)==0) {
            int i=std::atoi(e.id.c_str()+10);
            if (i>=0 && i<static_cast<int>(cfg.custom.size())) beginRadial(e.id,cfg.custom[i],e.time,false);
        } else if (e.id.rfind("MM_ACTION_",0)==0) execute(std::atoi(e.id.c_str()+10),true);
        // Nexus dispatches each callback on a separate thread. A release can arrive first.
        auto orphan=earlyReleases.find(e.id);
        if (radial.held && radial.bind==e.id && orphan!=earlyReleases.end() && e.time-orphan->second<=0.15) {
            pending.push_front({e.id,true,e.time});earlyReleases.erase(orphan);
        }
    }
    if (radial.waiting && (t-radial.pressedAt)*1000.0>cfg.tapMs) showRadial();
}
UINT wndProc(HWND,UINT message,WPARAM key,LPARAM data) {
    if (!accepting) return message;
    if ((message==WM_KEYDOWN || message==WM_SYSKEYDOWN) && !(data&(1LL<<30))) {
        if (key==VK_ESCAPE) { onBind("ESCAPE",false); if (targeting || captureRadial) return 0; }
        if (key==static_cast<WPARAM>(jumpVirtualKey.load())) onBind("JUMP",false);
    }
    if (targeting && message==WM_LBUTTONDOWN) { consumeConfirmUp=true; onBind("CONFIRM",false); return 0; }
    if (message==WM_LBUTTONUP && consumeConfirmUp.exchange(false)) return 0;
    if ((targeting || captureRadial) && (message==WM_LBUTTONUP || message==WM_RBUTTONDOWN || message==WM_RBUTTONUP)) return 0;
    return message;
}

void drawIcon(int a,ImVec2 position,float size,float opacity,bool selected) {
    auto* draw=ImGui::GetWindowDrawList();
    if (selected) draw->AddCircleFilled(ImVec2(position.x+size/2,position.y+size/2),size*0.61f,IM_COL32(220,170,50,160));
    Texture_t* texture=api->Textures_Get(textureIds[a].c_str());
    if (texture && texture->Resource) draw->AddImage(texture->Resource,position,ImVec2(position.x+size,position.y+size),ImVec2(0,0),ImVec2(1,1),ImGui::GetColorU32(ImVec4(1,1,1,opacity)));
    else {
        draw->AddCircleFilled(ImVec2(position.x+size/2,position.y+size/2),size/2,IM_COL32(40,45,60,220));
        draw->AddText(position,IM_COL32(255,255,255,255),name(a));
    }
}
void drawRadial() {
    if (!radial.visible) return;
    auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(display);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0,0,0,0));
    ImGui::Begin("Mounts radial##MM",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoBringToFrontOnFocus);
    int selected=selectedAction();
    float size=cfg.iconSize;
    float radius=std::min(cfg.radius,std::min(display.x,display.y)*0.43f);
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddCircleFilled(radial.origin,radius+size*0.7f,IM_COL32(10,14,25,150),64);
    for (size_t i=0;i<radial.actions.size();++i) {
        float angle=cfg.startAngle*Pi/180+(static_cast<float>(i)+0.5f)*2*Pi/radial.actions.size();
        ImVec2 position(radial.origin.x+std::cos(angle)*radius-size/2,radial.origin.y+std::sin(angle)*radius-size/2);
        drawIcon(radial.actions[i],position,size,selected==radial.actions[i]?1:cfg.opacity,selected==radial.actions[i]);
    }
    if (radial.center>=0) drawIcon(radial.center,ImVec2(radial.origin.x-size/2,radial.origin.y-size/2),size,selected==radial.center?1:cfg.opacity,selected==radial.center);
    std::string caption=radial.actions.empty() && radial.center<0 ? "No bound actions. Configure GW2/Nexus game binds." : radial.profile.name+"  |  "+name(selected);
    auto textSize=ImGui::CalcTextSize(caption.c_str());
    draw->AddText(ImVec2(radial.origin.x-textSize.x/2,radial.origin.y+radius+size),IM_COL32(255,255,255,255),caption.c_str());
    const auto mouse=ImGui::GetIO().MousePos;
    draw->AddTriangleFilled(mouse,ImVec2(mouse.x+3,mouse.y+17),ImVec2(mouse.x+12,mouse.y+12),IM_COL32(255,255,255,255));
    ImGui::End(); ImGui::PopStyleColor();
}
void drawBars() {
    if ((!state.gameplay && !cfg.showLoading) || !state.valid || state.mapOpen) return;
    for (size_t i=0;i<cfg.bars.size();++i) {
        auto& b=cfg.bars[i]; if (!b.enabled || (state.competitive && !b.competitive)) continue;
        ImGui::SetNextWindowPos(ImVec2(b.x,b.y),ImGuiCond_Once);
        ImGui::SetNextWindowBgAlpha(b.opacity*0.45f);
        auto title=b.name+"##MMBAR"+std::to_string(i);
        ImGuiWindowFlags flags=ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav;
        if (b.locked) flags|=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove;
        ImGui::Begin(title.c_str(),nullptr,flags);
        int n=0; for (int a:b.actions) if (bound(a)) {
            if (n++ && !b.vertical) ImGui::SameLine(0,4);
            ImGui::PushID(a); auto pos=ImGui::GetCursorScreenPos();
            bool click=ImGui::InvisibleButton("icon",ImVec2(b.size,b.size)); bool hover=ImGui::IsItemHovered();
            drawIcon(a,pos,b.size,b.opacity,hover);
            if (hover) ImGui::SetTooltip("%s",name(a));
            if (click) execute(a,true);
            ImGui::PopID();
        }
        if (!n) ImGui::TextUnformatted("Bind mounts in GW2 / Nexus first.");
        auto p=ImGui::GetWindowPos();
        if (p.x!=b.x || p.y!=b.y) { b.x=p.x;b.y=p.y;changed=true; }
        ImGui::End();
    }
}
void drawInfo() {
    if (!cfg.info || !state.valid || (!state.gameplay && !cfg.showLoading) || state.mapOpen) return;
    const bool queue=cfg.showQueue && engine.queued>=0;
    const bool later=cfg.showLater && engine.pending>=0;
    const bool ground=cfg.showGround && targetAction>=0;
    if (!queue && !later && !ground) return;
    ImGui::SetNextWindowPos(ImVec2(cfg.infoX,cfg.infoY),infoPositionRequested?ImGuiCond_Always:ImGuiCond_Once);
    infoPositionRequested=false;
    ImGuiWindowFlags flags=ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    if (cfg.infoLocked) flags|=ImGuiWindowFlags_NoMove;
    ImGui::Begin("Mounts status##MM",nullptr,flags);
    if (queue) ImGui::Text("Out of combat: %s",name(engine.queued));
    if (later) ImGui::Text("After loading: %s",name(engine.pending));
    if (ground) ImGui::Text("Click in game: %s",name(targetAction));
    if (ImGui::Button("Cancel pending action")) cancelAll();
    const auto position=ImGui::GetWindowPos();
    if (cfg.infoX!=position.x || cfg.infoY!=position.y) {cfg.infoX=position.x;cfg.infoY=position.y;changed=true;}
    ImGui::End();
}

void actionCombo(const char* label,int& value,const std::vector<int>* list=nullptr) {
    if (ImGui::BeginCombo(label,name(value))) {
        if (ImGui::Selectable("None",value<0)) {value=-1;changed=true;}
        for (int a=0;a<ActionCount;++a) if (!list || std::find(list->begin(),list->end(),a)!=list->end()) {
            if (ImGui::Selectable(name(a),a==value)) {value=a;changed=true;}
        }
        ImGui::EndCombo();
    }
}
void editActions(std::vector<int>& actions) {
    for (size_t i=0;i<actions.size();++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s %s",name(actions[i]),bound(actions[i])?"":"(not bound)"); ImGui::SameLine(220);
        if (ImGui::SmallButton("Up") && i>0) {std::swap(actions[i],actions[i-1]);changed=true;}
        ImGui::SameLine(); if (ImGui::SmallButton("Down") && i+1<actions.size()) {std::swap(actions[i],actions[i+1]);changed=true;}
        ImGui::SameLine(); bool remove=ImGui::SmallButton("Remove"); ImGui::PopID();
        if (remove) { actions.erase(actions.begin()+i);changed=true;break; }
    }
    if (ImGui::BeginCombo("Add action","Select...")) {
        for (int a=0;a<ActionCount;++a) if (std::find(actions.begin(),actions.end(),a)==actions.end() && ImGui::Selectable(name(a))) {actions.push_back(a);changed=true;}
        ImGui::EndCombo();
    }
}
void editProfile(RadialProfile& p,bool contextual) {
    changed|=ImGui::Checkbox("Enabled",&p.enabled);
    char text[128]{}; std::snprintf(text,sizeof(text),"%s",p.name.c_str());
    if (ImGui::InputText("Name",text,sizeof(text))) {p.name=text;changed=true;}
    const char* centers[]={"None","Default action","Last used"};
    changed|=ImGui::Combo("Center behavior",&p.centerBehavior,centers,3);
    actionCombo("Default action",p.defaultAction,&p.actions);
    changed|=ImGui::Checkbox("Remove center from ring",&p.removeCenter);
    if (contextual) {
        changed|=ImGui::Checkbox("Apply instantly if one action",&p.instantSingle);
        actionCombo("Tap action",p.tapAction,&p.actions);
        changed|=ImGui::Checkbox("Bypass combat queue for this context",&p.unconditional);
    }
    ImGui::Separator(); editActions(p.actions);
}

// Optional API request is explicit and never stores/logs the token.
void checkMastery() {
    if (masteryBusy || !apiToken[0]) return;
    if (masteryWorker.joinable()) masteryWorker.join();
    std::string token=apiToken; SecureZeroMemory(apiToken,sizeof(apiToken));
    masteryBusy=true;
    masteryWorker=std::thread([token=std::move(token)]() mutable {
        int result=-1;
        try {
        HINTERNET session=WinHttpOpen(L"MountsNexus/0.1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if (session) {
            WinHttpSetTimeouts(session,4000,4000,4000,4000);
            HINTERNET connection=WinHttpConnect(session,L"api.guildwars2.com",INTERNET_DEFAULT_HTTPS_PORT,0);
            if (connection) {
                HINTERNET request=WinHttpOpenRequest(connection,L"GET",L"/v2/account/masteries",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
                if (request) {
                    std::wstring header=L"Authorization: Bearer "+std::wstring(token.begin(),token.end());
                    if (WinHttpSendRequest(request,header.c_str(),static_cast<DWORD>(header.size()),nullptr,0,0,0) && WinHttpReceiveResponse(request,nullptr)) {
                        DWORD code=0,codeSize=sizeof(code); WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&codeSize,WINHTTP_NO_HEADER_INDEX);
                        std::string body; DWORD available=0;
                        while (code==200 && WinHttpQueryDataAvailable(request,&available) && available && body.size()+available<=1024*1024) {
                            std::string chunk(available,'\0'); DWORD count=0;
                            if (!WinHttpReadData(request,chunk.data(),available,&count) || count==0) break;
                            body.append(chunk.data(),count);
                        }
                        auto response=json::parse(body,nullptr,false);
                        if (code==200 && response.is_array()) {
                            result=0;
                            for (const auto& m:response) if (m.is_object() && m.contains("id") && m["id"].is_number_integer() &&
                                m.contains("level") && m["level"].is_number_integer() && m["id"].get<int>()==36 && m["level"].get<int>()>=4) result=1;
                        }
                    }
                    SecureZeroMemory(header.data(),header.size()*sizeof(wchar_t)); WinHttpCloseHandle(request);
                }
                WinHttpCloseHandle(connection);
            }
            WinHttpCloseHandle(session);
        }
        } catch (...) { result=-1; }
        SecureZeroMemory(token.data(),token.size()); masteryResult=result; masteryBusy=false;
    });
}

void drawSettings() {
    if (!settingsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(740,650),ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Mounts & More — Nexus##MM_SETTINGS",&settingsOpen)) {ImGui::End();return;}
    ImGui::TextUnformatted("Hold Ctrl+Alt+M: contextual radial | Ctrl+Shift+M: settings");
    ImGui::TextWrapped("Set game action keys in Guild Wars 2 and Nexus > Game Binds. Addon and custom radial shortcuts are in Nexus > Input Binds (MM_*).");
    if (ImGui::Button(changed?"Save settings *":"Save settings")) {save();updateQuickAccess();}
    ImGui::SameLine(); if (ImGui::Button("Cancel all pending actions")) cancelAll();
    if (!status.empty()) ImGui::TextWrapped("%s",status.c_str());
    if (ImGui::BeginTabBar("MM tabs")) {
        if (ImGui::BeginTabItem("General")) {
            changed|=ImGui::Checkbox("Out of combat queue (PvE only)",&cfg.queue);
            changed|=ImGui::Checkbox("Remount after loading (same character)",&cfg.autoRemount);
            changed|=ImGui::Checkbox("Combat Launch mastery enabled",&cfg.combatLaunch);
            ImGui::TextWrapped("Optional API verification: account + progression permissions. Token stays in memory for this request only.");
            ImGui::InputText("API token",apiToken,sizeof(apiToken),ImGuiInputTextFlags_Password);
            if (ImGui::Button(masteryBusy?"Checking...":"Verify Combat Launch")) checkMastery();
            int r=masteryResult.load();
            ImGui::SameLine(); ImGui::TextUnformatted(r==1?"Unlocked":r==0?"Not unlocked":r==-1?"API failed; manual setting applies":"Not checked; manual setting applies");
            changed|=ImGui::Checkbox("Show on loading screens",&cfg.showLoading);
            changed|=ImGui::Checkbox("Show pending action panel",&cfg.info);
            changed|=ImGui::Checkbox("Show out-of-combat indicator",&cfg.showQueue);
            changed|=ImGui::Checkbox("Show after-loading indicator",&cfg.showLater);
            changed|=ImGui::Checkbox("Show ground-target indicator",&cfg.showGround);
            changed|=ImGui::Checkbox("Lock status panel position",&cfg.infoLocked);
            float infoPosition[]={cfg.infoX,cfg.infoY};
            if (ImGui::DragFloat2("Status panel position",infoPosition,1,0,16000,"%.0f")) {
                cfg.infoX=infoPosition[0];cfg.infoY=infoPosition[1];infoPositionRequested=true;changed=true;
            }
            if (ImGui::Checkbox("Corner shortcuts via Nexus Quick Access",&cfg.quickAccess)) {changed=true;updateQuickAccess();}
            const char* ground[]={"Normal","Instant","Fast with range indicator"};
            if (ImGui::Combo("Ground targeting",&cfg.groundMode,ground,3)) {releaseTarget();changed=true;}
            ImGui::TextWrapped("Match the targeting mode in GW2. For delayed ground skills, click in the game to place; Escape cancels.");
            if (ImGui::InputInt("Jump key (Windows virtual-key code)",&cfg.jumpKey)) {cfg.jumpKey=std::clamp(cfg.jumpKey,1,255);jumpVirtualKey=cfg.jumpKey;changed=true;}
            ImGui::TextUnformatted("Space = 32. Set this to match your in-game jump key.");
            ImGui::Separator(); changed|=ImGui::Checkbox("Spawn radial at mouse",&cfg.spawnAtMouse);
            changed|=ImGui::Checkbox("Use default action without radial",&cfg.defaultOnly);
            changed|=ImGui::SliderFloat("Radius",&cfg.radius,60,450,"%.0f px");
            changed|=ImGui::SliderFloat("Icon size",&cfg.iconSize,20,120,"%.0f px");
            changed|=ImGui::SliderFloat("Opacity",&cfg.opacity,0.1f,1);
            changed|=ImGui::SliderFloat("First sector start",&cfg.startAngle,-360,360,"%.0f degrees");
            changed|=ImGui::SliderInt("Tap threshold (0 disables)",&cfg.tapMs,0,2000);
            changed|=ImGui::SliderFloat("Falling sample interval",&cfg.frequency,0.01f,1,"%.2f s");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Contextual radials")) {
            ImGui::TextWrapped("First enabled matching context wins, in this priority order.");
            for (int i=0;i<7;++i) { ImGui::PushID(i); if (ImGui::CollapsingHeader(contextNames[i])) editProfile(cfg.contexts[i],true); ImGui::PopID(); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Custom radials")) {
            if (cfg.custom.size()<32 && ImGui::Button("Add radial")) {RadialProfile p;p.name="Custom "+std::to_string(cfg.custom.size()+1);p.enabled=true;cfg.custom.push_back(p);registerBinds();changed=true;}
            for (size_t i=0;i<cfg.custom.size();++i) {
                if (cfg.custom[i].name.empty()) continue; // tombstone preserves the IDs/binds of other radials
                ImGui::PushID(static_cast<int>(i));
                bool remove=false;
                if (ImGui::CollapsingHeader(cfg.custom[i].name.c_str())) {
                    ImGui::Text("Nexus Input Bind: MM_RADIAL_%u",static_cast<unsigned>(i));
                    editProfile(cfg.custom[i],false); remove=ImGui::Button("Delete radial");
                }
                ImGui::PopID();
                if (remove) {closeRadial();cfg.custom[i]=RadialProfile{};cfg.custom[i].name.clear();cfg.custom[i].enabled=false;registerBinds();changed=true;break;}
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Icon bars")) {
            if (cfg.bars.size()<32 && ImGui::Button("Add icon bar")) {cfg.bars.push_back(Bar{});changed=true;}
            for (size_t i=0;i<cfg.bars.size();++i) {
                auto& b=cfg.bars[i]; ImGui::PushID(static_cast<int>(i)); bool remove=false;
                if (ImGui::CollapsingHeader((b.name+"##bar").c_str())) {
                    char text[128]{};std::snprintf(text,sizeof(text),"%s",b.name.c_str());
                    if (ImGui::InputText("Name",text,sizeof(text))) {b.name=text;changed=true;}
                    changed|=ImGui::Checkbox("Enabled",&b.enabled); changed|=ImGui::Checkbox("Vertical",&b.vertical);
                    changed|=ImGui::Checkbox("Enabled in competitive maps",&b.competitive); changed|=ImGui::Checkbox("Lock position / hide title",&b.locked);
                    changed|=ImGui::SliderFloat("Size",&b.size,20,120,"%.0f"); changed|=ImGui::SliderFloat("Opacity",&b.opacity,0.1f,1);
                    editActions(b.actions); remove=ImGui::Button("Delete bar");
                }
                ImGui::PopID(); if (remove) {cfg.bars.erase(cfg.bars.begin()+i);changed=true;break;}
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Icons / bindings")) {
            ImGui::TextWrapped("PNG files live in addons/MountsNexus/icons. Enter a filename, then reload icons. Existing GW2 bindings are used automatically.");
            if (ImGui::Button("Reload icons")) {refreshTextures();updateQuickAccess();}
            for (int i=0;i<ActionCount;++i) {
                ImGui::PushID(i); ImGui::Text("%s: %s (game bind %d)",name(i),bound(i)?"Bound":"Not bound",gameBinds[i]);
                char text[160]{};std::snprintf(text,sizeof(text),"%s",iconFiles[i].c_str());
                if (ImGui::InputText("PNG filename",text,sizeof(text))) {
                    std::string file=text;
                    if (file.find_first_of("/\\:")==std::string::npos && std::filesystem::path(file).extension()==".png") {iconFiles[i]=file;changed=true;}
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diagnostics")) {
            ImGui::Text("Mumble valid: %d | gameplay: %d | focused: %d",state.valid,state.gameplay,state.focused);
            ImGui::Text("Map %u type %u | combat %d | competitive %d",state.mapId,state.mapType,state.combat,state.competitive);
            ImGui::Text("Character: %s | mount: %d",state.character.c_str(),state.mount);
            ImGui::Text("Position: %.2f %.2f %.2f | water: %.2f",state.x,state.y,state.z,waterSurface(state));
            ImGui::Text("Falling: %d | context: %s",state.falling,contextNames[contextIndex(state)]);
            ImGui::TextWrapped("Local development build 0.1.0; unregistered signature -1296911960. Original authors: Manlaan, Bennieboj. Static reverse engineering evidence is included in the workspace docs.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
void optionsRender() { if (ImGui::Button("Open Mounts & More settings")) settingsOpen=true; }
void render() {
    if (!accepting) return;
    double t=now();
    state=readState(t);
    if (!inputAllowed() || (targeting && (state.character!=targetCharacter || state.mapId!=targetMap || !state.gameplay))) {
        releaseTarget(); closeRadial();
    }
    int pending=engine.update(state,options()); if (validAction(pending)) send(pending);
    processEvents(t);
    if (!radial.visible) restoreCamera();
    drawBars(); drawInfo(); drawRadial(); drawSettings();
    previous=state;
}
void load(AddonAPI_t* a) {
    api=a;
    try {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(reinterpret_cast<void*(*)(size_t,void*)>(api->ImguiMalloc),reinterpret_cast<void(*)(void*,void*)>(api->ImguiFree));
    const char* path=api->Paths_GetAddonDirectory("MountsNexus"); directory=path?path:"addons\\MountsNexus";
    std::filesystem::create_directories(directory+"\\icons"); configPath=directory+"\\settings.json";
    loadSettings(); registerBinds(); refreshTextures(); updateQuickAccess();
    accepting=true;
    api->WndProc_Register(wndProc);
    api->GUI_Register(RT_Render,render); api->GUI_Register(RT_OptionsRender,optionsRender);
    api->GUI_RegisterCloseOnEscape("Mounts & More — Nexus##MM_SETTINGS",&settingsOpen);
    log(LOGL_INFO,"Loaded Nexus port 0.1.0. Ctrl+Alt+M: radial; Ctrl+Shift+M: settings.");
    } catch (const std::exception& e) { accepting=false;log(LOGL_WARNING,std::string("Load failed: ")+e.what()); }
}
void unload() {
    accepting=false;
    api->GUI_Deregister(render);api->GUI_Deregister(optionsRender);api->WndProc_Deregister(wndProc);
    api->GUI_DeregisterCloseOnEscape("Mounts & More — Nexus##MM_SETTINGS");
    for (const auto& id:bindIds) api->InputBinds_Deregister(id.c_str());
    removeQuickAccess(); cancelAll();
    if (masteryWorker.joinable()) masteryWorker.join();
    SecureZeroMemory(apiToken,sizeof(apiToken));
    if (changed && configWritable) save();
    {std::lock_guard<std::mutex> guard(eventMutex);events.clear();}
    log(LOGL_INFO,"Unloaded; pending actions cancelled.");
}
AddonDefinition_t definition{Signature,NEXUS_API_VERSION,"Mounts & More (Fedora port)",{0,1,0,0},
    "Manlaan, Bennieboj; Nexus adaptation","Contextual mounts, mastery skills and novelties",load,unload,AF_None,UP_None,nullptr};
} // namespace
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {return &definition;}
