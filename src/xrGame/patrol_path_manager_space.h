////////////////////////////////////////////////////////////////////////////
//	Module 		: patrol_path_manager_space.h
//	Created 	: 03.12.2003
//  Modified 	: 03.12.2003
//	Author		: Dmitriy Iassenev
//	Description : Patrol path manager space
////////////////////////////////////////////////////////////////////////////

#pragma once

namespace PatrolPathManager
{
	enum EPatrolStartType : int
	{
		ePatrolStartTypeFirst = u32(0),
		ePatrolStartTypeLast,
		ePatrolStartTypeNearest,
		ePatrolStartTypePoint,
		ePatrolStartTypeNext,
		ePatrolStartTypeDummy = -1,
	};

	enum EPatrolRouteType : int
	{
		ePatrolRouteTypeStop = u32(0),
		ePatrolRouteTypeContinue,
		ePatrolRouteTypeDummy = -1,
	};
};
