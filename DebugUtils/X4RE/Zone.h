#pragma once

#include "Macro.h"
#include "Sector.h"

namespace X4RE
{
    class Zone
    {
    public:
        void* vTable;                                               //0x0000
        char pad_0000[88];                                          //0x0008
        Macro* macro;                                               //0x0060
        Sector* sector;                                             //0x0068
    };
}
