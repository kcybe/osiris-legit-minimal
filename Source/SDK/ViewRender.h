#pragma once

#include "Pad.h"
#include <Platform/PlatformSpecific.h>

namespace csgo
{

struct ViewRender {
    PAD(WIN32_LINUX(0x588, 0x648))
    float smokeOverlayAmount;
};

}
