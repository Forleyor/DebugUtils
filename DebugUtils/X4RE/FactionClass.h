#pragma once
#include <string>

class FactionClass
{
public:
    void* vTable;                                               //0x0000
    std::string factionID;                                      //0x0008 - example: "argon"
    std::string factionName;                                    //0x0028 - ID from t file (example: {20203,201} "Argon Federation")
    std::string factionDescription;                             //0x0048 - ID from t file (example: {20203,202})
    char pad_0068[16];                                          //0x0068
    std::string factionTag;                                     //0x0078 - ID from t file (example: {20203,204} "ARG")
    std::string factionEntity;                                  //0x0098 - ID from t file (example: {20203,204} "Federation")
    char pad_00B8[64];                                          //0x00B8
    std::string factionID;                                      //0x00F8
    std::string factionID2;                                     //0x0118
    std::string factionID3;                                     //0x0138
};