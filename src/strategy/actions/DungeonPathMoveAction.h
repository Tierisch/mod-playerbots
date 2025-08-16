
#ifndef _PLAYERBOT_DUNGEONPATHMOVEACTION_H
#define _PLAYERBOT_DUNGEONPATHMOVEACTION_H

#include "MovementActions.h"
#include "DungeonWaypointMgr.h"
#include "PlayerbotAI.h"


class DungeonPathMoveAction : public MovementAction {
public:
    DungeonPathMoveAction(PlayerbotAI* ai, DungeonWaypointMgr* mgr);
    bool Execute(Event event) override;

private:
    DungeonWaypointMgr* waypointMgr;
};

#endif
