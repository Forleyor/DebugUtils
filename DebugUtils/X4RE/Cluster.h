#pragma once

#include "Macro.h"
#include "Galaxy.h"

namespace X4RE
{
    class Cluster
    {
    public:

        void* vTable;                                               //0x0000
        char pad_0000[88];                                          //0x0008
        Macro* macro;                                               //0x0060
        Galaxy* Galaxy;                                             //0x0068
    };
}