#include "DungeonPathStrategy.h"
#include "DungeonPathMoveAction.h"
#include "DungeonWaypointMgr.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"


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
        Player* bot = botAI->GetBot();
        Group* group = bot->GetGroup();
        Player* mainTank = nullptr;
        Player* tankInSubgroup = nullptr;
        Player* tankInRaid = nullptr;
        bool isBotMainTank = false;

        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsAlive() || bot->GetMapId() != member->GetMapId())
                    continue;
                if (PlayerbotAI::IsMainTank(member))
                {
                    mainTank = member;
                    break; // Highest priority
                }
                if (botAI->IsTank(member))
                {
                    if (ref->getSubGroup() == bot->GetSubGroup() && !tankInSubgroup)
                        tankInSubgroup = member;
                    else if (!tankInRaid)
                        tankInRaid = member;
                }
            }
            // Check if bot is the highest priority tank
            if (mainTank && mainTank == bot)
                isBotMainTank = true;
            else if (!mainTank && tankInSubgroup && tankInSubgroup == bot)
                isBotMainTank = true;
            else if (!mainTank && !tankInSubgroup && tankInRaid && tankInRaid == bot)
                isBotMainTank = true;
        }
        else
        {
            // No group, bot leads
            isBotMainTank = botAI->IsTank(bot);
        }

        if (isBotMainTank)
            return NextAction::array(0, new NextAction("dungeon path move", 1.0f), nullptr);
        else
            return NextAction::array(0, new NextAction("follow", 1.0f), nullptr);
}

void DungeonPathStrategy::InitTriggers(std::vector<TriggerNode*>& triggers) {}
