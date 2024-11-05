#pragma once

#include "Macro.h"
#include "Cluster.h"

namespace X4RE
{
    class Sector
    {
    public:
        void* vTable;                                               //0x0000
        char pad_0000[88];                                          //0x0008
        Macro* macro;                                               //0x0060
        Cluster* cluster;                                           //0x0068
    };
}
