
#ifndef _PLAYERBOT_DUNGEONPATHMOVEACTION_H
#define _PLAYERBOT_DUNGEONPATHMOVEACTION_H

#include "MovementActions.h"
#include "DungeonWaypointMgr.h"
#include "PlayerbotAI.h"
#include <chrono>

class DungeonPathMoveAction : public MovementAction
{
public:
    DungeonPathMoveAction(PlayerbotAI* ai, DungeonWaypointMgr* mgr);
    bool Execute(Event event) override;

private:
    static constexpr float WAYPOINT_REACHED_DISTANCE = 6.0f;
    static constexpr float RESUME_PATH_DISTANCE = 40.0f;            // Max distance from previous waypoint to resume path instead of closest
    static constexpr size_t RESUME_SEARCH_RANGE = 3;                // Number of waypoints to search ahead of previous when resuming path before just picking closest
    static constexpr float NPC_INTERACT_DISTANCE = 20.0f;
    static constexpr uint32_t NOTIFICATION_COOLDOWN_SECONDS = 10;   // Time before sending a waypoint notification or starting a waypoint pause again (prevents repeats when bots haven't fully moved past that waypoint)
    static constexpr uint32_t WAIT_FOR_GROUP_DELAY_MS = 3000;       // Time before rechecking group conditions when waiting (for healer mana, distance, etc.)

    size_t FindClosestWaypoint(const DungeonPath* path, Player* bot) const;
    size_t DetermineTargetIndex(const DungeonPath* path, Player* bot, size_t closestIndex, float distToClosest);
    bool CheckGroupConditions(Player* bot, const DungeonWaypoint& waypoint) const;
    void HandleWaypointInteraction(const DungeonWaypoint& waypoint, Player* bot);
    void HandleWaypointNotification(const DungeonWaypoint& waypoint);
    void HandleCombatEngagement(Player* bot, Event event);

    DungeonWaypointMgr* waypointMgr;
    size_t previousIndex = 0;
    std::chrono::steady_clock::time_point lastNotifyTime = std::chrono::steady_clock::now() - std::chrono::seconds(30);
};

#endif
