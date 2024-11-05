#pragma once
#include <string>

#include "FactionClass.h"
#include "Macro.h"
#include "TemplateClass.h"
#include "Zone.h"

namespace X4RE
{
    class Station
    {
    public:
        void* vTable;                                               //0x0000
        char pad_0000[80];                                          //0x0008
        TemplateComponent* templateComponent;                       //0x0058
        Macro* macro;                                               //0x0060
        Zone* zone;                                                 //0x0068
        char pad_0068[664];                                         //0x0070
        std::string stationName;                                    //0x0308
        char pad_0328[32];                                          //0x0328
        char code[8];                                               //0x0348 - the 6 digit code (example: "ZC5-187")
        char pad_0350[24];                                          //0x0350
        FactionClass* factionClass;                                 //0x0368
        char pad_0370[592];                                         //0x0370
        std::string stationTypeName;                                //0x05C0
    };
}