#pragma once
#include <string>

class Macro
{
public:
	void* vTable;												//0x0000
	char pad_0000[24];											//0x0008
	std::string macroName;										//0x0020
};