#pragma once

#include "Aimbot.h"
#include "Backtrack.h"
#include "Visuals.h"
#include "Misc.h"
#include <InventoryChanger/InventoryChanger.h>

#include <Interfaces/OtherInterfaces.h>

struct Features {
    Aimbot aimbot;
    Backtrack backtrack;
    Visuals visuals;
    inventory_changer::InventoryChanger inventoryChanger;
    Misc misc;
};

[[nodiscard]] inline Features createFeatures(const Memory& memory, const ClientInterfaces& clientInterfaces, const EngineInterfaces& engineInterfaces, const OtherInterfaces& otherInterfaces, const helpers::PatternFinder& clientPatternFinder, const helpers::PatternFinder& enginePatternFinder)
{
    return Features{
        .backtrack = Backtrack{ otherInterfaces.getCvar() },
        .visuals{ memory, otherInterfaces, clientInterfaces, engineInterfaces, clientPatternFinder, enginePatternFinder },
        .inventoryChanger{ inventory_changer::createInventoryChanger(otherInterfaces, memory) },
        .misc{ clientInterfaces, otherInterfaces, memory, clientPatternFinder, enginePatternFinder }
    };
}
