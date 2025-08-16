#include "DungeonPathStrategy.h"
#include "DungeonPathMoveAction.h"
#include "DungeonWaypointMgr.h"
#include "PlayerbotAI.h"


DungeonPathStrategy::DungeonPathStrategy(PlayerbotAI* ai)
    : NonCombatStrategy(ai)
{
    waypointMgr = new DungeonWaypointMgr();
    waypointMgr->LoadWaypoints();
}

DungeonPathStrategy::~DungeonPathStrategy()
{
    delete waypointMgr;
}

NextAction** DungeonPathStrategy::getDefaultActions()
{
    return NextAction::array(0, new NextAction("dungeon path move", 1.0f), nullptr);
}

void DungeonPathStrategy::InitTriggers(std::vector<TriggerNode*>& triggers) {}
