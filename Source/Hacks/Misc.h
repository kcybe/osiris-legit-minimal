#pragma once

#include "../JsonForward.h"
#include "../Memory.h"
#include <InventoryChanger/InventoryChanger.h>
#include <Platform/IsPlatform.h>
#include <Interfaces/ClientInterfaces.h>

namespace csgo { enum class FrameStage; }

class GameEvent;
struct ImDrawList;
struct UserCmd;
class EngineInterfaces;
class Glow;
class Visuals;
struct DemoPlaybackParameters;

class Misc {
public:
    Misc(const ClientInterfaces& clientInterfaces, const helpers::PatternFinder& clientPatternFinder)
        : clientInterfaces{ clientInterfaces }
    {
#if IS_WIN32()
        demoOrHLTV = ReturnAddress{ clientPatternFinder("\x84\xC0\x75\x09\x38\x05").get() };
        money = clientPatternFinder("\x84\xC0\x75\x0C\x5B").get();
        insertIntoTree = ReturnAddress{ clientPatternFinder("\x56\x52\xFF\x50\x18").add(5).get() };
        demoFileEndReached = ReturnAddress{ clientPatternFinder("\x8B\xC8\x85\xC9\x74\x1F\x80\x79\x10").get() };
#elif IS_LINUX()
        demoOrHLTV = ReturnAddress{ clientPatternFinder("\x0F\xB6\x10\x89\xD0").add(-16).get() };
        money = clientPatternFinder("\x84\xC0\x75\x9E\xB8????\xEB\xB9").get();
        insertIntoTree = ReturnAddress{ clientPatternFinder("\x74\x24\x4C\x8B\x10").add(31).get() };
        demoFileEndReached = ReturnAddress{ clientPatternFinder("\x48\x85\xC0\x0F\x84????\x80\x78\x10?\x74\x7F").get() };
#endif
    }

    bool isRadarHackOn() noexcept;
    bool isMenuKeyPressed() noexcept;
    float maxAngleDelta() noexcept;
    float aspectRatio() noexcept;

    void edgejump(UserCmd* cmd) noexcept;
    void slowwalk(UserCmd* cmd) noexcept;
    void updateClanTag(const Memory& memory, bool = false) noexcept;
    void spectatorList() noexcept;
    void noscopeCrosshair(const Memory& memory, ImDrawList* drawlist) noexcept;
    void recoilCrosshair(const Memory& memory, ImDrawList* drawList) noexcept;
    void watermark(const Memory& memory) noexcept;
    void prepareRevolver(const Engine& engine, const Memory& memory, UserCmd*) noexcept;
    void fastPlant(const EngineTrace& engineTrace, const OtherInterfaces& interfaces, UserCmd*) noexcept;
    void fastStop(UserCmd*) noexcept;
    void drawBombTimer(const Memory& memory) noexcept;
    void stealNames(const Engine& engine, const OtherInterfaces& interfaces, const Memory& memory) noexcept;
    void disablePanoramablur(const OtherInterfaces& interfaces) noexcept;
    void quickReload(const OtherInterfaces& interfaces, UserCmd*) noexcept;
    bool changeName(const Engine& engine, const OtherInterfaces& interfaces, const Memory& memory, bool, const char*, float) noexcept;
    void bunnyHop(UserCmd*) noexcept;
    void fakeBan(const Engine& engine, const OtherInterfaces& interfaces, const Memory& memory, bool = false) noexcept;
    void nadePredict(const OtherInterfaces& interfaces) noexcept;
    void fixTabletSignal() noexcept;
    void killMessage(const Engine& engine, const GameEvent& event) noexcept;
    void fixMovement(UserCmd* cmd, float yaw) noexcept;
    void antiAfkKick(UserCmd* cmd) noexcept;
    void fixAnimationLOD(const Engine& engine, const Memory& memory, csgo::FrameStage stage) noexcept;
    void autoPistol(const Memory& memory, UserCmd* cmd) noexcept;
    void chokePackets(const Engine& engine, bool& sendPacket) noexcept;
    void autoReload(UserCmd* cmd) noexcept;
    void revealRanks(UserCmd* cmd) noexcept;
    void autoStrafe(UserCmd* cmd) noexcept;
    void removeCrouchCooldown(UserCmd* cmd) noexcept;
    void moonwalk(UserCmd* cmd) noexcept;
    void playHitSound(const Engine& engine, const GameEvent& event) noexcept;
    void killSound(const Engine& engine, const GameEvent& event) noexcept;
    void purchaseList(const Engine& engine, const OtherInterfaces& interfaces, const Memory& memory, const GameEvent* event = nullptr) noexcept;
    void oppositeHandKnife(const OtherInterfaces& interfaces, csgo::FrameStage stage) noexcept;
    void runReportbot(const Engine& engine, const OtherInterfaces& interfaces, const Memory& memory) noexcept;
    void resetReportbot() noexcept;
    void preserveKillfeed(const Memory& memory, bool roundStart = false) noexcept;
    void voteRevealer(const OtherInterfaces& interfaces, const Memory& memory, const GameEvent& event) noexcept;
    void onVoteStart(const OtherInterfaces& interfaces, const Memory& memory, const void* data, int size) noexcept;
    void onVotePass(const Memory& memory) noexcept;
    void onVoteFailed(const Memory& memory) noexcept;
    void drawOffscreenEnemies(const Engine& engine, const Memory& memory, ImDrawList* drawList) noexcept;
    void autoAccept(const OtherInterfaces& interfaces, const Memory& memory, const char* soundEntry) noexcept;

    bool isPlayingDemoHook(ReturnAddress returnAddress, std::uintptr_t frameAddress) const;
    const DemoPlaybackParameters* getDemoPlaybackParametersHook(ReturnAddress returnAddress, const DemoPlaybackParameters& demoPlaybackParameters) const;
    std::optional<std::pair<Vector, Vector>> listLeavesInBoxHook(ReturnAddress returnAddress, std::uintptr_t frameAddress) const;

    void updateEventListeners(const EngineInterfaces& engineInterfaces, bool forceRemove = false) noexcept;
    void updateInput() noexcept;

    // GUI
    void menuBarItem() noexcept;
    void tabItem(Visuals& visuals, inventory_changer::InventoryChanger& inventoryChanger, Glow& glow, const EngineInterfaces& engineInterfaces, const OtherInterfaces& interfaces, const Memory& memory) noexcept;
    void drawGUI(Visuals& visuals, inventory_changer::InventoryChanger& inventoryChanger, Glow& glow, const EngineInterfaces& engineInterfaces, const OtherInterfaces& interfaces, const Memory& memory, bool contentOnly) noexcept;

    // Config
    json toJson() noexcept;
    void fromJson(const json& j) noexcept;
    void resetConfig() noexcept;

private:
    ClientInterfaces clientInterfaces;

    ReturnAddress demoOrHLTV;
    std::uintptr_t money;
    ReturnAddress insertIntoTree;
    ReturnAddress demoFileEndReached;
};
