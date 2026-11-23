#include "StdAfx.h" // xrGame precompiled header (Windows found it case-insensitively)
#pragma hdrstop

#include "LevelGameDef.h"

xr_token rpoint_type[] = {
	{"Actor Spawn", rptActorSpawn},
	{"Artefact Spawn", rptArtefactSpawn},
	{"Item Spawn", rptItemSpawn},
	{0, 0}
};
