#include "GlobalContext.h"

#if IS_WIN32()
#include <imgui/imgui_impl_dx9.h>
#include <imgui/imgui_impl_win32.h>

#include "Platform/Windows/DynamicLibrarySection.h"
#else
#include <imgui/imgui_impl_sdl.h>
#include <imgui/imgui_impl_opengl3.h>

#include "Platform/Linux/DynamicLibrarySection.h"
#endif

#include "EventListener.h"
#include "GameData.h"
#include "GUI.h"
#include "Hooks.h"
#include "InventoryChanger/InventoryChanger.h"
#include "Memory.h"
#include "Hacks/Aimbot.h"
#include "Hacks/Backtrack.h"
#include "Hacks/Chams.h"
#include "Hacks/EnginePrediction.h"
#include "Hacks/Misc.h"
#include "Hacks/Sound.h"
#include "Hacks/StreamProofESP.h"
#include "Hacks/Visuals.h"
#include "SDK/ClientClass.h"
#include "SDK/Constants/ClassId.h"
#include "SDK/Constants/FrameStage.h"
#include "SDK/Constants/UserMessages.h"
#include "SDK/Engine.h"
#include "SDK/Entity.h"
#include "SDK/EntityList.h"
#include "SDK/GlobalVars.h"
#include "SDK/InputSystem.h"
#include "SDK/LocalPlayer.h"
#include "SDK/ModelRender.h"
#include "SDK/Recv.h"
#include "SDK/SoundEmitter.h"
#include "SDK/SoundInfo.h"
#include "SDK/StudioRender.h"
#include "SDK/Surface.h"
#include "SDK/UserCmd.h"
#include "SDK/ViewSetup.h"
#include "SDK/PODs/RenderableInfo.h"

#include "Interfaces/ClientInterfaces.h"

GlobalContext::GlobalContext()
{
#if IS_WIN32()
    const windows_platform::DynamicLibrary clientDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
    const windows_platform::DynamicLibrary engineDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
#elif IS_LINUX()
    const linux_platform::SharedObject clientDLL{ linux_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
    const linux_platform::SharedObject engineDLL{ linux_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
#endif

    retSpoofGadgets.emplace(helpers::PatternFinder{ getCodeSection(clientDLL.getView()) }, helpers::PatternFinder{ getCodeSection(engineDLL.getView()) });
}

bool GlobalContext::createMoveHook(float inputSampleTime, UserCmd* cmd)
{
    auto result = hooks->clientMode.callOriginal<bool, WIN32_LINUX(24, 25)>(inputSampleTime, cmd);

    if (!cmd->commandNumber)
        return result;

#if IS_WIN32()
    // bool& sendPacket = *reinterpret_cast<bool*>(*reinterpret_cast<std::uintptr_t*>(FRAME_ADDRESS()) - 0x1C);
    // since 19.02.2022 game update sendPacket is no longer on stack
    bool sendPacket = true;
#else
    bool dummy;
    bool& sendPacket = dummy;
#endif
    static auto previousViewAngles{ cmd->viewangles };
    const auto currentViewAngles{ cmd->viewangles };

    memory->globalVars->serverTime(cmd);
    features->misc.nadePredict();
    features->misc.antiAfkKick(cmd);
    features->visuals.removeShadows();
    features->misc.runReportbot(getEngineInterfaces().getEngine());
    features->misc.bunnyHop(cmd);
    features->misc.updateClanTag();
    features->misc.fakeBan(getEngineInterfaces().getEngine());
    features->misc.stealNames(getEngineInterfaces().getEngine());
    features->misc.revealRanks(cmd);
    features->misc.fixTabletSignal();

    EnginePrediction::run(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, *memory, cmd);

    features->aimbot.run(features->misc, getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *config, *memory, cmd);
    features->backtrack.run(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), *memory, cmd);

    if (!(cmd->buttons & (UserCmd::IN_ATTACK | UserCmd::IN_ATTACK2))) {
        features->misc.chokePackets(getEngineInterfaces().getEngine(), sendPacket);
    }

    auto viewAnglesDelta{ cmd->viewangles - previousViewAngles };
    viewAnglesDelta.normalize();
    viewAnglesDelta.x = std::clamp(viewAnglesDelta.x, -features->misc.maxAngleDelta(), features->misc.maxAngleDelta());
    viewAnglesDelta.y = std::clamp(viewAnglesDelta.y, -features->misc.maxAngleDelta(), features->misc.maxAngleDelta());

    cmd->viewangles = previousViewAngles + viewAnglesDelta;

    cmd->viewangles.normalize();
    features->misc.fixMovement(cmd, currentViewAngles.y);

    cmd->viewangles.x = std::clamp(cmd->viewangles.x, -89.0f, 89.0f);
    cmd->viewangles.y = std::clamp(cmd->viewangles.y, -180.0f, 180.0f);
    cmd->viewangles.z = 0.0f;
    cmd->forwardmove = std::clamp(cmd->forwardmove, -450.0f, 450.0f);
    cmd->sidemove = std::clamp(cmd->sidemove, -450.0f, 450.0f);

    previousViewAngles = cmd->viewangles;

    return false;
}

void GlobalContext::doPostScreenEffectsHook(void* param)
{
    if (getEngineInterfaces().getEngine().isInGame()) {
        features->visuals.thirdperson();
        features->visuals.inverseRagdollGravity();
        features->visuals.reduceFlashEffect();
        features->visuals.updateBrightness();
        features->visuals.remove3dSky();
    }
    hooks->clientMode.callOriginal<void, WIN32_LINUX(44, 45)>(param);
}

float GlobalContext::getViewModelFovHook()
{
    float additionalFov = features->visuals.viewModelFov();
    if (localPlayer) {
        if (const auto activeWeapon = Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon()); activeWeapon.getPOD() != nullptr && activeWeapon.getNetworkable().getClientClass()->classId == ClassId::Tablet)
            additionalFov = 0.0f;
    }

    return hooks->clientMode.callOriginal<float, WIN32_LINUX(35, 36)>() + additionalFov;
}

void GlobalContext::drawModelExecuteHook(void* ctx, void* state, const ModelRenderInfo& info, matrix3x4* customBoneToWorld)
{
    if (getOtherInterfaces().getStudioRender().isForcedMaterialOverride())
        return hooks->modelRender.callOriginal<void, 21>(ctx, state, std::cref(info), customBoneToWorld);

    if (features->visuals.removeHands(info.model->name) || features->visuals.removeSleeves(info.model->name) || features->visuals.removeWeapons(info.model->name))
        return;

    if (static Chams chams; !chams.render(features->backtrack, getEngineInterfaces().getEngine(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *memory, *config, ctx, state, info, customBoneToWorld))
        hooks->modelRender.callOriginal<void, 21>(ctx, state, std::cref(info), customBoneToWorld);

    getOtherInterfaces().getStudioRender().forcedMaterialOverride(nullptr);
}

int GlobalContext::svCheatsGetIntHook(void* _this, ReturnAddress returnAddress)
{
    const auto original = hooks->svCheats.getOriginal<int, WIN32_LINUX(13, 16)>()(_this);
    if (features->visuals.svCheatsGetBoolHook(returnAddress))
        return 1;
    return original;
}

void GlobalContext::frameStageNotifyHook(csgo::FrameStage stage)
{
    if (getEngineInterfaces().getEngine().isConnected() && !getEngineInterfaces().getEngine().isInGame())
        features->misc.changeName(getEngineInterfaces().getEngine(), true, nullptr, 0.0f);

    if (stage == csgo::FrameStage::START)
        GameData::update(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), *memory);

    if (stage == csgo::FrameStage::RENDER_START) {
        features->misc.disablePanoramablur();
        features->visuals.colorWorld();
        features->misc.updateEventListeners(getEngineInterfaces());
        features->visuals.updateEventListeners();
    }
    if (getEngineInterfaces().getEngine().isInGame()) {
        features->visuals.skybox(stage);
        features->visuals.removeBlur(stage);
        features->misc.oppositeHandKnife(stage);
        features->visuals.removeGrass(stage);
        features->visuals.modifySmoke(stage);
        features->visuals.disablePostProcessing(stage);
        features->visuals.removeVisualRecoil(stage);
        features->visuals.applyZoom(stage);
        features->misc.fixAnimationLOD(getEngineInterfaces().getEngine(), stage);
        features->backtrack.update(getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *memory, stage);
    }
    features->inventoryChanger.run(getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *memory, stage);

    hooks->client.callOriginal<void, 37>(stage);
}

int GlobalContext::emitSoundHook(void* filter, int entityIndex, int channel, const char* soundEntry, unsigned int soundEntryHash, const char* sample, float volume, int seed, int soundLevel, int flags, int pitch, const Vector& origin, const Vector& direction, void* utlVecOrigins, bool updatePositions, float soundtime, int speakerentity, void* soundParams)
{
    Sound::modulateSound(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, *memory, soundEntry, entityIndex, volume);
    features->misc.autoAccept(soundEntry);

    volume = std::clamp(volume, 0.0f, 1.0f);
    return hooks->sound.callOriginal<int, WIN32_LINUX(5, 6)>(filter, entityIndex, channel, soundEntry, soundEntryHash, sample, volume, seed, soundLevel, flags, pitch, std::cref(origin), std::cref(direction), utlVecOrigins, updatePositions, soundtime, speakerentity, soundParams);
}

bool GlobalContext::shouldDrawFogHook(ReturnAddress returnAddress)
{
#if IS_WIN32()
    if constexpr (std::is_same_v<HookType, MinHook>) {
        if (returnAddress != memory->shouldDrawFogReturnAddress)
            return hooks->clientMode.callOriginal<bool, 17>();
    }
#endif

    return !features->visuals.shouldRemoveFog();
}

bool GlobalContext::shouldDrawViewModelHook()
{
    if (features->visuals.isZoomOn() && localPlayer && localPlayer.get().fov() < 45 && localPlayer.get().fovStart() < 45)
        return false;
    return hooks->clientMode.callOriginal<bool, WIN32_LINUX(27, 28)>();
}

void GlobalContext::lockCursorHook()
{
    if (gui->isOpen())
        return getOtherInterfaces().getSurface().unlockCursor();
    return hooks->surface.callOriginal<void, 67>();
}

void GlobalContext::setDrawColorHook(int r, int g, int b, int a, ReturnAddress returnAddress)
{
    features->visuals.setDrawColorHook(returnAddress, a);
    hooks->surface.callOriginal<void, WIN32_LINUX(15, 14)>(r, g, b, a);
}

void GlobalContext::overrideViewHook(ViewSetup* setup)
{
    if (localPlayer && !localPlayer.get().isScoped())
        setup->fov += features->visuals.fov();
    setup->farZ += features->visuals.farZ() * 10;
    hooks->clientMode.callOriginal<void, WIN32_LINUX(18, 19)>(setup);
}

int GlobalContext::dispatchSoundHook(SoundInfo& soundInfo)
{
    if (const char* soundName = getOtherInterfaces().getSoundEmitter().getSoundName(soundInfo.soundIndex)) {
        Sound::modulateSound(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, *memory, soundName, soundInfo.entityIndex, soundInfo.volume);
        soundInfo.volume = std::clamp(soundInfo.volume, 0.0f, 1.0f);
    }
    return hooks->originalDispatchSound(soundInfo);
}

void GlobalContext::render2dEffectsPreHudHook(void* viewSetup)
{
    hooks->viewRender.callOriginal<void, WIN32_LINUX(39, 40)>(viewSetup);
}

const DemoPlaybackParameters* GlobalContext::getDemoPlaybackParametersHook(ReturnAddress returnAddress)
{
    const auto params = hooks->engine.callOriginal<const DemoPlaybackParameters*, WIN32_LINUX(218, 219)>();

    if (params)
        return features->misc.getDemoPlaybackParametersHook(returnAddress, *params);

    return params;
}

bool GlobalContext::dispatchUserMessageHook(csgo::UserMessageType type, int passthroughFlags, int size, const void* data)
{
    features->misc.dispatchUserMessageHook(type, size, data);
    if (type == csgo::UserMessageType::Text)
        features->inventoryChanger.onUserTextMsg(*memory, data, size);

    return hooks->client.callOriginal<bool, 38>(type, passthroughFlags, size, data);
}

bool GlobalContext::isPlayingDemoHook(ReturnAddress returnAddress, std::uintptr_t frameAddress)
{
    const auto result = hooks->engine.callOriginal<bool, 82>();

    if (features->misc.isPlayingDemoHook(returnAddress, frameAddress))
        return true;

    return result;
}

void GlobalContext::updateColorCorrectionWeightsHook()
{
    hooks->clientMode.callOriginal<void, WIN32_LINUX(58, 61)>();
    features->visuals.updateColorCorrectionWeightsHook();
}

float GlobalContext::getScreenAspectRatioHook(int width, int height)
{
    if (features->misc.aspectRatio() != 0.0f)
        return features->misc.aspectRatio();
    return hooks->engine.callOriginal<float, 101>(width, height);
}

void GlobalContext::renderSmokeOverlayHook(bool update)
{
    if (!features->visuals.renderSmokeOverlayHook())
        hooks->viewRender.callOriginal<void, WIN32_LINUX(41, 42)>(update);  
}

double GlobalContext::getArgAsNumberHook(void* params, int index, ReturnAddress returnAddress)
{
    const auto result = hooks->panoramaMarshallHelper.callOriginal<double, 5>(params, index);
    features->inventoryChanger.getArgAsNumberHook(static_cast<int>(result), returnAddress);
    return result;
}

const char* GlobalContext::getArgAsStringHook(void* params, int index, ReturnAddress returnAddress)
{
    const auto result = hooks->panoramaMarshallHelper.callOriginal<const char*, 7>(params, index);

    if (result)
        features->inventoryChanger.getArgAsStringHook(*memory, result, returnAddress, params);

    return result;
}

void GlobalContext::setResultIntHook(void* params, int result, ReturnAddress returnAddress)
{
    result = features->inventoryChanger.setResultIntHook(returnAddress, params, result);
    hooks->panoramaMarshallHelper.callOriginal<void, WIN32_LINUX(14, 11)>(params, result);
}

unsigned GlobalContext::getNumArgsHook(void* params, ReturnAddress returnAddress)
{
    const auto result = hooks->panoramaMarshallHelper.callOriginal<unsigned, 1>(params);
    features->inventoryChanger.getNumArgsHook(result, returnAddress, params);
    return result;
}

void GlobalContext::updateInventoryEquippedStateHook(std::uintptr_t inventory, csgo::ItemId itemID, csgo::Team team, int slot, bool swap)
{
    features->inventoryChanger.onItemEquip(team, slot, itemID);
    hooks->inventoryManager.callOriginal<void, WIN32_LINUX(29, 30)>(inventory, itemID, team, slot, swap);
}

void GlobalContext::soUpdatedHook(SOID owner, csgo::pod::SharedObject* object, int event)
{
    features->inventoryChanger.onSoUpdated(SharedObject::from(retSpoofGadgets->client, object));
    hooks->inventory.callOriginal<void, 1>(owner, object, event);
}

int GlobalContext::listLeavesInBoxHook(const Vector& mins, const Vector& maxs, unsigned short* list, int listMax, ReturnAddress returnAddress, std::uintptr_t frameAddress)
{
    if (const auto newVectors = features->misc.listLeavesInBoxHook(returnAddress, frameAddress))
        return hooks->bspQuery.callOriginal<int, 6>(std::cref(newVectors->first), std::cref(newVectors->second), list, listMax);
    return hooks->bspQuery.callOriginal<int, 6>(std::cref(mins), std::cref(maxs), list, listMax);
}

#if IS_WIN32()
LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void* GlobalContext::allocKeyValuesMemoryHook(int size, ReturnAddress returnAddress)
{
    if (returnAddress == memory->keyValuesAllocEngine || returnAddress == memory->keyValuesAllocClient)
        return nullptr;
    return hooks->keyValuesSystem.callOriginal<void*, 2>(size);
}

LRESULT GlobalContext::wndProcHook(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (state == GlobalContext::State::Initialized) {
        ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam);
        getOtherInterfaces().getInputSystem().enableInput(!gui->isOpen());
    } else if (state == GlobalContext::State::NotInitialized) {
        state = GlobalContext::State::Initializing;

        const windows_platform::DynamicLibrary clientDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
        clientInterfaces = createClientInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ clientDLL.getView(), retSpoofGadgets->client } });
        const windows_platform::DynamicLibrary engineDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
        engineInterfacesPODs = createEngineInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ engineDLL.getView(), retSpoofGadgets->client } });
        interfaces.emplace();

        memory.emplace(helpers::PatternFinder{ getCodeSection(clientDLL.getView()) }, helpers::PatternFinder{ getCodeSection(engineDLL.getView()) }, clientInterfaces->client, *retSpoofGadgets);

        Netvars::init(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getClient());
        gameEventListener.emplace(getEngineInterfaces().getGameEventManager(memory->getEventDescriptor));

        ImGui::CreateContext();
        ImGui_ImplWin32_Init(window);

        features.emplace(createFeatures(*memory, ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), helpers::PatternFinder{ getCodeSection(clientDLL.getView()) }, helpers::PatternFinder{ getCodeSection(engineDLL.getView()) }));
        config.emplace(features->misc, features->inventoryChanger, features->backtrack, features->visuals, getOtherInterfaces(), *memory);
        gui.emplace();
        hooks->install(clientInterfaces->client, getEngineInterfaces(), getOtherInterfaces(), *memory);

        state = GlobalContext::State::Initialized;
    }

    return CallWindowProcW(hooks->originalWndProc, window, msg, wParam, lParam);
}

HRESULT GlobalContext::presentHook(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND windowOverride, const RGNDATA* dirtyRegion)
{
    [[maybe_unused]] static bool imguiInit{ ImGui_ImplDX9_Init(device) };

    if (config->loadScheduledFonts())
        ImGui_ImplDX9_DestroyFontsTexture();

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();

    renderFrame();

    if (device->BeginScene() == D3D_OK) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }

    //
    GameData::clearUnusedAvatars();
    features->inventoryChanger.clearUnusedItemIconTextures();
    //

    return hooks->originalPresent(device, src, dest, windowOverride, dirtyRegion);
}

HRESULT GlobalContext::resetHook(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    features->inventoryChanger.clearItemIconTextures();
    GameData::clearTextures();
    return hooks->originalReset(device, params);
}

#else
int GlobalContext::pollEventHook(SDL_Event* event)
{
    const auto result = hooks->pollEvent(event);

    if (state == GlobalContext::State::Initialized) {
        if (result && ImGui_ImplSDL2_ProcessEvent(event) && gui->isOpen())
            event->type = 0;
    } else if (state == GlobalContext::State::NotInitialized) {
        state = GlobalContext::State::Initializing;

        const linux_platform::SharedObject clientSo{ linux_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
        clientInterfaces = createClientInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ clientSo.getView(), retSpoofGadgets->client } });
        const linux_platform::SharedObject engineSo{ linux_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
        engineInterfacesPODs = createEngineInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ engineSo.getView(), retSpoofGadgets->client } });

        interfaces.emplace();
        memory.emplace(helpers::PatternFinder{ linux_platform::getCodeSection(clientSo.getView()) }, helpers::PatternFinder{ linux_platform::getCodeSection(engineSo.getView()) }, clientInterfaces->client, *retSpoofGadgets);

        Netvars::init(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getClient());
        gameEventListener.emplace(getEngineInterfaces().getGameEventManager(memory->getEventDescriptor));

        ImGui::CreateContext();

        features.emplace(createFeatures(*memory, ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), helpers::PatternFinder{ linux_platform::getCodeSection(clientSo.getView()) }, helpers::PatternFinder{ linux_platform::getCodeSection(engineSo.getView()) }));
        config.emplace(features->misc, features->inventoryChanger, features->backtrack, features->visuals, getOtherInterfaces(), *memory);
        
        gui.emplace();
        hooks->install(clientInterfaces->client, getEngineInterfaces(), getOtherInterfaces(), *memory);

        state = GlobalContext::State::Initialized;
    }
    
    return result;
}

void GlobalContext::swapWindowHook(SDL_Window* window)
{
    [[maybe_unused]] static const auto _ = ImGui_ImplSDL2_InitForOpenGL(window, nullptr);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);

    renderFrame();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    GameData::clearUnusedAvatars();
    features->inventoryChanger.clearUnusedItemIconTextures();

    hooks->swapWindow(window);
}

#endif

void GlobalContext::viewModelSequenceNetvarHook(recvProxyData& data, void* outStruct, void* arg3)
{
    const auto viewModel = Entity::from(retSpoofGadgets->client, static_cast<csgo::pod::Entity*>(outStruct));

    if (localPlayer && ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getEntityList().getEntityFromHandle(viewModel.owner()) == localPlayer.get().getPOD()) {
        if (const auto weapon = Entity::from(retSpoofGadgets->client, ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getEntityList().getEntityFromHandle(viewModel.weapon())); weapon.getPOD() != nullptr) {
            if (weapon.getNetworkable().getClientClass()->classId == ClassId::Deagle && data.value._int == 7)
                data.value._int = 8;

            features->inventoryChanger.fixKnifeAnimation(weapon, data.value._int);
        }
    }

    proxyHooks.viewModelSequence.originalProxy(data, outStruct, arg3);
}

void GlobalContext::spottedHook(recvProxyData& data, void* outStruct, void* arg3)
{
    if (features->misc.isRadarHackOn()) {
        data.value._int = 1;

        if (localPlayer) {
            const auto entity = Entity::from(retSpoofGadgets->client, static_cast<csgo::pod::Entity*>(outStruct));
            if (const auto index = localPlayer.get().getNetworkable().index(); index > 0 && index <= 32)
                entity.spottedByMask() |= 1 << (index - 1);
        }
    }

    proxyHooks.spotted.originalProxy(data, outStruct, arg3);
}

void GlobalContext::fireGameEventCallback(csgo::pod::GameEvent* eventPointer)
{
    const auto event = GameEvent::from(retSpoofGadgets->client, eventPointer);

    switch (fnv::hashRuntime(event.getName())) {
    case fnv::hash("round_start"):
        GameData::clearProjectileList();
        [[fallthrough]];
    case fnv::hash("round_freeze_end"):
        features->misc.purchaseList(getEngineInterfaces().getEngine(), &event);
        break;
    case fnv::hash("player_death"):
        features->inventoryChanger.updateStatTrak(getEngineInterfaces().getEngine(), event);
        features->inventoryChanger.overrideHudIcon(getEngineInterfaces().getEngine(), *memory, event);
        features->misc.killMessage(getEngineInterfaces().getEngine(), event);
        features->misc.killSound(getEngineInterfaces().getEngine(), event);
        break;
    case fnv::hash("player_hurt"):
        features->misc.playHitSound(getEngineInterfaces().getEngine(), event);
        features->visuals.hitMarker(&event);
        break;
    case fnv::hash("vote_cast"):
        features->misc.voteRevealer(event);
        break;
    case fnv::hash("round_mvp"):
        features->inventoryChanger.onRoundMVP(getEngineInterfaces().getEngine(), event);
        break;
    case fnv::hash("item_purchase"):
        features->misc.purchaseList(getEngineInterfaces().getEngine(), &event);
        break;
    case fnv::hash("bullet_impact"):
        features->visuals.bulletTracer(event);
        break;
    }
}

void GlobalContext::renderFrame()
{
    ImGui::NewFrame();

    if (const auto& displaySize = ImGui::GetIO().DisplaySize; displaySize.x > 0.0f && displaySize.y > 0.0f) {
        StreamProofESP::render(*memory, *config);
        features->misc.purchaseList(getEngineInterfaces().getEngine());
        features->misc.noscopeCrosshair(ImGui::GetBackgroundDrawList());
        features->misc.recoilCrosshair(ImGui::GetBackgroundDrawList());
        features->misc.drawBombTimer();
        features->misc.spectatorList();
        features->visuals.hitMarker(nullptr, ImGui::GetBackgroundDrawList());
        features->misc.watermark();

        features->aimbot.updateInput(*config);
        features->visuals.updateInput();
        StreamProofESP::updateInput(*config);
        features->misc.updateInput();
        Chams::updateInput(*config);

        gui->handleToggle(features->misc, getOtherInterfaces());

        if (gui->isOpen())
            gui->render(features->misc, features->inventoryChanger, features->backtrack, features->visuals, getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *memory, *config);
    }

    ImGui::EndFrame();
    ImGui::Render();
}
