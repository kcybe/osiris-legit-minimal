#include <array>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui/imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui/imgui_internal.h>

#include "../ConfigStructs.h"
#include <fnv.h>
#include "../GameData.h"
#include "../Helpers.h"
#include "../Memory.h"
#include "../imguiCustom.h"
#include "Visuals.h"

#include <SDK/PODs/ConVar.h>
#include <SDK/ConVar.h>
#include <SDK/Cvar.h>
#include <SDK/Engine.h>
#include <SDK/Entity.h>
#include <SDK/EntityList.h>
#include <SDK/Constants/ConVarNames.h>
#include <SDK/Constants/FrameStage.h>
#include <SDK/GameEvent.h>
#include <SDK/GlobalVars.h>
#include <SDK/Input.h>
#include <SDK/LocalPlayer.h>
#include <SDK/Material.h>
#include <SDK/MaterialSystem.h>
#include <SDK/ViewRender.h>
#include <SDK/ViewRenderBeams.h>

#include "../GlobalContext.h"
#include <Interfaces/ClientInterfaces.h>
#include <Config/LoadConfigurator.h>
#include <Config/ResetConfigurator.h>
#include <Config/SaveConfigurator.h>

struct BulletTracers : ColorToggle {
    BulletTracers() : ColorToggle{ 0.0f, 0.75f, 1.0f, 1.0f } {}
};

struct VisualsConfig {
    KeyBindToggle zoomKey;
    bool thirdperson{ false };
    KeyBindToggle thirdpersonKey;
    int thirdpersonDistance{ 0 };
    int viewmodelFov{ 0 };
    int fov{ 0 };
    int farZ{ 0 };
    int flashReduction{ 0 };
    float brightness{ 0.0f };
    ColorToggle3 world;
    ColorToggle3 sky;
    int hitMarker{ 0 };
    float hitMarkerTime{ 0.6f };
    BulletTracers bulletTracers;
    ColorToggle molotovHull{ 1.0f, 0.27f, 0.0f, 0.3f };
} visualsConfig;

static void from_json(const json& j, BulletTracers& o)
{
    from_json(j, static_cast<ColorToggle&>(o));
}

static void from_json(const json& j, VisualsConfig& v)
{
    read(j, "Zoom key", v.zoomKey);
    read(j, "Thirdperson", v.thirdperson);
    read(j, "Thirdperson key", v.thirdpersonKey);
    read(j, "Thirdperson distance", v.thirdpersonDistance);
    read(j, "Viewmodel FOV", v.viewmodelFov);
    read(j, "FOV", v.fov);
    read(j, "Far Z", v.farZ);
    read(j, "Flash reduction", v.flashReduction);
    read(j, "Brightness", v.brightness);
    read<value_t::object>(j, "World", v.world);
    read<value_t::object>(j, "Sky", v.sky);
    read(j, "Hit marker", v.hitMarker);
    read(j, "Hit marker time", v.hitMarkerTime);
    read<value_t::object>(j, "Bullet Tracers", v.bulletTracers);
    read<value_t::object>(j, "Molotov Hull", v.molotovHull);
}

static void to_json(json& j, const BulletTracers& o, const BulletTracers& dummy = {})
{
    to_json(j, static_cast<const ColorToggle&>(o), dummy);
}

static void to_json(json& j, VisualsConfig& o)
{
    const VisualsConfig dummy;

    WRITE("Zoom key", zoomKey);
    WRITE("Thirdperson", thirdperson);
    WRITE("Thirdperson key", thirdpersonKey);
    WRITE("Thirdperson distance", thirdpersonDistance);
    WRITE("Viewmodel FOV", viewmodelFov);
    WRITE("FOV", fov);
    WRITE("Far Z", farZ);
    WRITE("Flash reduction", flashReduction);
    WRITE("Brightness", brightness);
    WRITE("World", world);
    WRITE("Sky", sky);
    WRITE("Hit marker", hitMarker);
    WRITE("Hit marker time", hitMarkerTime);
    WRITE("Bullet Tracers", bulletTracers);
    WRITE("Molotov Hull", molotovHull);
}

bool Visuals::isZoomOn() noexcept
{
    return zoom;
}

bool Visuals::shouldRemoveFog() noexcept
{
    return noFog;
}

float Visuals::viewModelFov() noexcept
{
    return static_cast<float>(visualsConfig.viewmodelFov);
}

float Visuals::fov() noexcept
{
    return static_cast<float>(visualsConfig.fov);
}

float Visuals::farZ() noexcept
{
    return static_cast<float>(visualsConfig.farZ);
}

void Visuals::inverseRagdollGravity() noexcept
{
    static auto ragdollGravity = interfaces.getCvar().findVar(csgo::cl_ragdoll_gravity);
    ConVar::from(retSpoofGadgets->client, ragdollGravity).setValue(inverseRagdollGravity_ ? -600 : 600);
}

void Visuals::colorWorld() noexcept
{
    if (!visualsConfig.world.enabled && !visualsConfig.sky.enabled)
        return;

    for (short h = interfaces.getMaterialSystem().firstMaterial(); h != interfaces.getMaterialSystem().invalidMaterial(); h = interfaces.getMaterialSystem().nextMaterial(h)) {
        const auto mat = Material::from(retSpoofGadgets->client, interfaces.getMaterialSystem().getMaterial(h));

        if (mat.getPOD() == nullptr || !mat.isPrecached())
            continue;

        const std::string_view textureGroup = mat.getTextureGroupName();

        if (visualsConfig.world.enabled && (textureGroup.starts_with("World") || textureGroup.starts_with("StaticProp"))) {
            if (visualsConfig.world.asColor3().rainbow)
                mat.colorModulate(rainbowColor(memory.globalVars->realtime, visualsConfig.world.asColor3().rainbowSpeed));
            else
                mat.colorModulate(visualsConfig.world.asColor3().color);
        } else if (visualsConfig.sky.enabled && textureGroup.starts_with("SkyBox")) {
            if (visualsConfig.sky.asColor3().rainbow)
                mat.colorModulate(rainbowColor(memory.globalVars->realtime, visualsConfig.sky.asColor3().rainbowSpeed));
            else
                mat.colorModulate(visualsConfig.sky.asColor3().color);
        }
    }
}

void Visuals::modifySmoke(csgo::FrameStage stage) noexcept
{
    if (stage != csgo::FrameStage::RENDER_START && stage != csgo::FrameStage::RENDER_END)
        return;

    static constexpr std::array smokeMaterials{
        "particle/vistasmokev1/vistasmokev1_emods",
        "particle/vistasmokev1/vistasmokev1_emods_impactdust",
        "particle/vistasmokev1/vistasmokev1_fire",
        "particle/vistasmokev1/vistasmokev1_smokegrenade"
    };

    for (const auto mat : smokeMaterials) {
        const auto material = Material::from(retSpoofGadgets->client, interfaces.getMaterialSystem().findMaterial(mat));
        material.setMaterialVarFlag(MaterialVarFlag::NO_DRAW, stage == csgo::FrameStage::RENDER_START && noSmoke);
        material.setMaterialVarFlag(MaterialVarFlag::WIREFRAME, stage == csgo::FrameStage::RENDER_START && wireframeSmoke);
    }
}

void Visuals::thirdperson() noexcept
{
    if (!visualsConfig.thirdperson)
        return;

    memory.input->isCameraInThirdPerson = (!visualsConfig.thirdpersonKey.isSet() || visualsConfig.thirdpersonKey.isToggled()) && localPlayer && localPlayer.get().isAlive();
    memory.input->cameraOffset.z = static_cast<float>(visualsConfig.thirdpersonDistance); 
}

void Visuals::removeVisualRecoil(csgo::FrameStage stage) noexcept
{
    if (!localPlayer || !localPlayer.get().isAlive())
        return;

    static Vector aimPunch;
    static Vector viewPunch;

    if (stage == csgo::FrameStage::RENDER_START) {
        aimPunch = localPlayer.get().aimPunchAngle();
        viewPunch = localPlayer.get().viewPunchAngle();

        if (noAimPunch)
            localPlayer.get().aimPunchAngle() = Vector{ };

        if (noViewPunch)
            localPlayer.get().viewPunchAngle() = Vector{ };

    } else if (stage == csgo::FrameStage::RENDER_END) {
        localPlayer.get().aimPunchAngle() = aimPunch;
        localPlayer.get().viewPunchAngle() = viewPunch;
    }
}

void Visuals::removeBlur(csgo::FrameStage stage) noexcept
{
    if (stage != csgo::FrameStage::RENDER_START && stage != csgo::FrameStage::RENDER_END)
        return;

    static auto blur = Material::from(retSpoofGadgets->client, interfaces.getMaterialSystem().findMaterial("dev/scope_bluroverlay"));
    blur.setMaterialVarFlag(MaterialVarFlag::NO_DRAW, stage == csgo::FrameStage::RENDER_START && noBlur);
}

void Visuals::updateBrightness() noexcept
{
    static auto brightness = interfaces.getCvar().findVar(csgo::mat_force_tonemap_scale);
    ConVar::from(retSpoofGadgets->client, brightness).setValue(visualsConfig.brightness);
}

void Visuals::removeGrass(csgo::FrameStage stage) noexcept
{
    if (stage != csgo::FrameStage::RENDER_START && stage != csgo::FrameStage::RENDER_END)
        return;

    auto getGrassMaterialName = [this]() noexcept -> const char* {
        switch (fnv::hashRuntime(engineInterfaces.getEngine().getLevelName())) {
        case fnv::hash("dz_blacksite"): return "detail/detailsprites_survival";
        case fnv::hash("dz_sirocco"): return "detail/dust_massive_detail_sprites";
        case fnv::hash("coop_autumn"): return "detail/autumn_detail_sprites";
        case fnv::hash("dz_frostbite"): return "ski/detail/detailsprites_overgrown_ski";
        // dz_junglety has been removed in 7/23/2020 patch
        // case fnv::hash("dz_junglety"): return "detail/tropical_grass";
        default: return nullptr;
        }
    };

    if (const auto grassMaterialName = getGrassMaterialName())
        Material::from(retSpoofGadgets->client, interfaces.getMaterialSystem().findMaterial(grassMaterialName)).setMaterialVarFlag(MaterialVarFlag::NO_DRAW, stage == csgo::FrameStage::RENDER_START && noGrass);
}

void Visuals::remove3dSky() noexcept
{
    static auto sky = interfaces.getCvar().findVar(csgo::r_3dsky);
    ConVar::from(retSpoofGadgets->client, sky).setValue(!no3dSky);
}

void Visuals::removeShadows() noexcept
{
    static auto shadows = interfaces.getCvar().findVar(csgo::cl_csm_enabled);
    ConVar::from(retSpoofGadgets->client, shadows).setValue(!noShadows);
}

void Visuals::applyZoom(csgo::FrameStage stage) noexcept
{
    if (zoom && localPlayer) {
        if (stage == csgo::FrameStage::RENDER_START && (localPlayer.get().fov() == 90 || localPlayer.get().fovStart() == 90)) {
            if (visualsConfig.zoomKey.isToggled()) {
                localPlayer.get().fov() = 40;
                localPlayer.get().fovStart() = 40;
            }
        }
    }
}

#if IS_WIN32()
#undef xor
#define DRAW_SCREEN_EFFECT(material, memory, engine) \
{ \
    const auto drawFunction = memory.drawScreenEffectMaterial; \
    int w, h; \
    engine.getScreenSize(w, h); \
    __asm { \
        __asm push h \
        __asm push w \
        __asm push 0 \
        __asm xor edx, edx \
        __asm mov ecx, material \
        __asm call drawFunction \
        __asm add esp, 12 \
    } \
}

#else
#define DRAW_SCREEN_EFFECT(material, memory, engine) \
{ \
    int w, h; \
    engine.getScreenSize(w, h); \
    reinterpret_cast<void(*)(csgo::pod::Material*, int, int, int, int)>(memory.drawScreenEffectMaterial)(material, 0, 0, w, h); \
}
#endif

void Visuals::hitMarker(const GameEvent* event, ImDrawList* drawList) noexcept
{
    if (visualsConfig.hitMarker == 0)
        return;

    static float lastHitTime = 0.0f;

    if (event) {
        if (localPlayer && event->getInt("attacker") == localPlayer.get().getUserId(engineInterfaces.getEngine()))
            lastHitTime = memory.globalVars->realtime;
        return;
    }

    if (lastHitTime + visualsConfig.hitMarkerTime < memory.globalVars->realtime)
        return;

    switch (visualsConfig.hitMarker) {
    case 1:
        const auto& mid = ImGui::GetIO().DisplaySize / 2.0f;
        constexpr auto color = IM_COL32(255, 255, 255, 255);
        drawList->AddLine({ mid.x - 10, mid.y - 10 }, { mid.x - 4, mid.y - 4 }, color);
        drawList->AddLine({ mid.x + 10.5f, mid.y - 10.5f }, { mid.x + 4.5f, mid.y - 4.5f }, color);
        drawList->AddLine({ mid.x + 10.5f, mid.y + 10.5f }, { mid.x + 4.5f, mid.y + 4.5f }, color);
        drawList->AddLine({ mid.x - 10, mid.y + 10 }, { mid.x - 4, mid.y + 4 }, color);
        break;
    }
}

void Visuals::disablePostProcessing(csgo::FrameStage stage) noexcept
{
    postProcessingDisabler.run(stage);
}

void Visuals::reduceFlashEffect() noexcept
{
    if (localPlayer && visualsConfig.flashReduction != 0)
        localPlayer.get().flashMaxAlpha() = 255.0f - visualsConfig.flashReduction * 2.55f;
}

bool Visuals::removeHands(const char* modelName) noexcept
{
    return noHands && std::strstr(modelName, "arms") && !std::strstr(modelName, "sleeve");
}

bool Visuals::removeSleeves(const char* modelName) noexcept
{
    return noSleeves && std::strstr(modelName, "sleeve");
}

bool Visuals::removeWeapons(const char* modelName) noexcept
{
    return noWeapons && std::strstr(modelName, "models/weapons/v_")
        && !std::strstr(modelName, "arms") && !std::strstr(modelName, "tablet")
        && !std::strstr(modelName, "parachute") && !std::strstr(modelName, "fists");
}

void Visuals::skybox(csgo::FrameStage stage) noexcept
{
    skyboxChanger.run(stage);
}

void Visuals::bulletTracer(const GameEvent& event) noexcept
{
    if (!visualsConfig.bulletTracers.enabled)
        return;

    if (!localPlayer)
        return;

    if (event.getInt("userid") != localPlayer.get().getUserId(engineInterfaces.getEngine()))
        return;

    const auto activeWeapon = Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon());
    if (activeWeapon.getPOD() == nullptr)
        return;

    BeamInfo beamInfo;

    if (!localPlayer.get().shouldDraw()) {
        const auto viewModel = Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntityFromHandle(localPlayer.get().viewModel()));
        if (viewModel.getPOD() == nullptr)
            return;

        if (!viewModel.getAttachment(activeWeapon.getMuzzleAttachmentIndex1stPerson(viewModel.getPOD()), beamInfo.start))
            return;
    } else {
        const auto worldModel = Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntityFromHandle(activeWeapon.weaponWorldModel()));
        if (worldModel.getPOD() == nullptr)
            return;

        if (!worldModel.getAttachment(activeWeapon.getMuzzleAttachmentIndex3rdPerson(), beamInfo.start))
            return;
    }

    beamInfo.end.x = event.getFloat("x");
    beamInfo.end.y = event.getFloat("y");
    beamInfo.end.z = event.getFloat("z");

    beamInfo.modelName = "sprites/physbeam.vmt";
    beamInfo.modelIndex = -1;
    beamInfo.haloName = nullptr;
    beamInfo.haloIndex = -1;

    beamInfo.red = 255.0f * visualsConfig.bulletTracers.asColor4().color[0];
    beamInfo.green = 255.0f * visualsConfig.bulletTracers.asColor4().color[1];
    beamInfo.blue = 255.0f * visualsConfig.bulletTracers.asColor4().color[2];
    beamInfo.brightness = 255.0f * visualsConfig.bulletTracers.asColor4().color[3];

    beamInfo.type = 0;
    beamInfo.life = 0.0f;
    beamInfo.amplitude = 0.0f;
    beamInfo.segments = -1;
    beamInfo.renderable = true;
    beamInfo.speed = 0.2f;
    beamInfo.startFrame = 0;
    beamInfo.frameRate = 0.0f;
    beamInfo.width = 2.0f;
    beamInfo.endWidth = 2.0f;
    beamInfo.flags = 0x40;
    beamInfo.fadeLength = 20.0f;

    if (const auto beam = memory.viewRenderBeams.createBeamPoints(beamInfo)) {
        constexpr auto FBEAM_FOREVER = 0x4000;
        beam->flags &= ~FBEAM_FOREVER;
        beam->die = memory.globalVars->currenttime + 2.0f;
    }
}

void Visuals::setDrawColorHook(ReturnAddress hookReturnAddress, int& alpha) const noexcept
{
    scopeOverlayRemover.setDrawColorHook(hookReturnAddress, alpha);
}

void Visuals::updateColorCorrectionWeightsHook() const noexcept
{
    colorCorrection.run(memory.clientMode);
    scopeOverlayRemover.updateColorCorrectionWeightsHook();
}

bool Visuals::svCheatsGetBoolHook(ReturnAddress hookReturnAddress) const noexcept
{
    return visualsConfig.thirdperson && hookReturnAddress == cameraThink;
}

bool Visuals::renderSmokeOverlayHook() const noexcept
{
    if (noSmoke || wireframeSmoke) {
        memory.viewRender->smokeOverlayAmount = 0.0f;
        return true;
    }
    return false;
}

void Visuals::updateEventListeners(bool forceRemove) noexcept
{
    static DefaultEventListener listener;
    static bool listenerRegistered = false;

    if (visualsConfig.bulletTracers.enabled && !listenerRegistered) {
        engineInterfaces.getGameEventManager(memory.getEventDescriptor).addListener(&listener, "bullet_impact");
        listenerRegistered = true;
    } else if ((!visualsConfig.bulletTracers.enabled || forceRemove) && listenerRegistered) {
        engineInterfaces.getGameEventManager(memory.getEventDescriptor).removeListener(&listener);
        listenerRegistered = false;
    }
}

void Visuals::updateInput() noexcept
{
    visualsConfig.thirdpersonKey.handleToggle();
    visualsConfig.zoomKey.handleToggle();
}

static bool windowOpen = false;

void Visuals::menuBarItem() noexcept
{
    if (ImGui::MenuItem("Visuals")) {
        windowOpen = true;
        ImGui::SetWindowFocus("Visuals");
        ImGui::SetWindowPos("Visuals", { 100.0f, 100.0f });
    }
}

void Visuals::tabItem() noexcept
{
    if (ImGui::BeginTabItem("Visuals")) {
        drawGUI(true);
        ImGui::EndTabItem();
    }
}

void Visuals::drawGUI(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!windowOpen)
            return;
        ImGui::SetNextWindowSize({ 680.0f, 0.0f });
        ImGui::Begin("Visuals", &windowOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnOffset(1, 280.0f);
    ImGui::Checkbox("Disable post-processing", &postProcessingDisabler.enabled);
    ImGui::Checkbox("Inverse ragdoll gravity", &inverseRagdollGravity_);
    ImGui::Checkbox("No fog", &noFog);
    ImGui::Checkbox("No 3d sky", &no3dSky);
    ImGui::Checkbox("No aim punch", &noAimPunch);
    ImGui::Checkbox("No view punch", &noViewPunch);
    ImGui::Checkbox("No hands", &noHands);
    ImGui::Checkbox("No sleeves", &noSleeves);
    ImGui::Checkbox("No weapons", &noWeapons);
    ImGui::Checkbox("No smoke", &noSmoke);
    ImGui::Checkbox("No blur", &noBlur);
    ImGui::Checkbox("No scope overlay", &scopeOverlayRemover.enabled);
    ImGui::Checkbox("No grass", &noGrass);
    ImGui::Checkbox("No shadows", &noShadows);
    ImGui::Checkbox("Wireframe smoke", &wireframeSmoke);
    ImGui::NextColumn();
    ImGui::Checkbox("Zoom", &zoom);
    ImGui::SameLine();
    ImGui::PushID("Zoom Key");
    ImGui::hotkey("", visualsConfig.zoomKey);
    ImGui::PopID();
    ImGui::Checkbox("Thirdperson", &visualsConfig.thirdperson);
    ImGui::SameLine();
    ImGui::PushID("Thirdperson Key");
    ImGui::hotkey("", visualsConfig.thirdpersonKey);
    ImGui::PopID();
    ImGui::PushItemWidth(290.0f);
    ImGui::PushID(0);
    ImGui::SliderInt("", &visualsConfig.thirdpersonDistance, 0, 1000, "Thirdperson distance: %d");
    ImGui::PopID();
    ImGui::PushID(1);
    ImGui::SliderInt("", &visualsConfig.viewmodelFov, -60, 60, "Viewmodel FOV: %d");
    ImGui::PopID();
    ImGui::PushID(2);
    ImGui::SliderInt("", &visualsConfig.fov, -60, 60, "FOV: %d");
    ImGui::PopID();
    ImGui::PushID(3);
    ImGui::SliderInt("", &visualsConfig.farZ, 0, 2000, "Far Z: %d");
    ImGui::PopID();
    ImGui::PushID(4);
    ImGui::SliderInt("", &visualsConfig.flashReduction, 0, 100, "Flash reduction: %d%%");
    ImGui::PopID();
    ImGui::PushID(5);
    ImGui::SliderFloat("", &visualsConfig.brightness, 0.0f, 1.0f, "Brightness: %.2f");
    ImGui::PopID();
    ImGui::PopItemWidth();
    ImGui::Combo("Skybox", &skyboxChanger.skybox, SkyboxChanger::skyboxList.data(), SkyboxChanger::skyboxList.size());
    ImGuiCustom::colorPicker("World color", visualsConfig.world);
    ImGuiCustom::colorPicker("Sky color", visualsConfig.sky);
    ImGui::Combo("Hit marker", &visualsConfig.hitMarker, "None\0Default (Cross)\0");
    ImGui::SliderFloat("Hit marker time", &visualsConfig.hitMarkerTime, 0.1f, 1.5f, "%.2fs");
    ImGuiCustom::colorPicker("Bullet Tracers", visualsConfig.bulletTracers.asColor4().color.data(), &visualsConfig.bulletTracers.asColor4().color[3], nullptr, nullptr, &visualsConfig.bulletTracers.enabled);
    ImGuiCustom::colorPicker("Molotov Hull", visualsConfig.molotovHull);

    ImGui::Checkbox("Color correction", &colorCorrection.enabled);
    ImGui::SameLine();

    if (bool ccPopup = ImGui::Button("Edit"))
        ImGui::OpenPopup("##popup");

    if (ImGui::BeginPopup("##popup")) {
        ImGui::VSliderFloat("##1", { 40.0f, 160.0f }, &colorCorrection.blue, 0.0f, 1.0f, "Blue\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##2", { 40.0f, 160.0f }, &colorCorrection.red, 0.0f, 1.0f, "Red\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##3", { 40.0f, 160.0f }, &colorCorrection.mono, 0.0f, 1.0f, "Mono\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##4", { 40.0f, 160.0f }, &colorCorrection.saturation, 0.0f, 1.0f, "Sat\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##5", { 40.0f, 160.0f }, &colorCorrection.ghost, 0.0f, 1.0f, "Ghost\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##6", { 40.0f, 160.0f }, &colorCorrection.green, 0.0f, 1.0f, "Green\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##7", { 40.0f, 160.0f }, &colorCorrection.yellow, 0.0f, 1.0f, "Yellow\n%.3f"); ImGui::SameLine();
        ImGui::EndPopup();
    }
    ImGui::Columns(1);

    if (!contentOnly)
        ImGui::End();
}

json Visuals::toJson() noexcept
{
    // old way
    json j;
    to_json(j, visualsConfig);
    
    // new way
    SaveConfigurator saveConfigurator;
    configure(saveConfigurator);
    if (const auto saveJson = saveConfigurator.getJson(); saveJson.is_object())
        j.update(saveJson);

    // temporary, until skyboxChanger is saved as a json object
    SaveConfigurator skyboxChangerConfigurator;
    skyboxChanger.configure(skyboxChangerConfigurator);
    if (const auto skyboxJson = skyboxChangerConfigurator.getJson(); skyboxJson.is_object())
        j.update(skyboxJson);
    return j;
}

void Visuals::fromJson(const json& j) noexcept
{
    from_json(j, visualsConfig);

    LoadConfigurator configurator{ j };
    skyboxChanger.configure(configurator);
    configure(configurator);
}

void Visuals::resetConfig() noexcept
{
    visualsConfig = {};
    ResetConfigurator resetConfigurator;
    configure(resetConfigurator);

    skyboxChanger.configure(resetConfigurator);
}
