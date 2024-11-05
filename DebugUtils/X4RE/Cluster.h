#pragma once

class Cluster
{
public:

	void* vTable;												//0x0000
	char pad_0000[88];											//0x0008
	class Macro* macro;											//0x0060
	class Galaxy* Galaxy;										//0x0068
};