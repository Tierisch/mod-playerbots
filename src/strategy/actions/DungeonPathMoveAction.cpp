#include "AiObjectContext.h"
#include "DungeonPathMoveAction.h"
#include "DungeonWaypointMgr.h"
#include "GenericActions.h"
#include "AttackersValue.h"
#include "GridNotifiers.h"
#include "GossipHelloAction.h"

DungeonPathMoveAction::DungeonPathMoveAction(PlayerbotAI* ai, DungeonWaypointMgr* mgr)
    : MovementAction(ai, "dungeon path move"), waypointMgr(mgr) {}

bool DungeonPathMoveAction::Execute(Event event) {
    Player* bot = botAI->GetBot();
    uint32 mapId = bot->GetMapId();

    // Get the first available path for this mapId
    const DungeonPath* path = nullptr;
    if (waypointMgr) {
        const auto& allPaths = waypointMgr->GetAllPaths();
        auto it = allPaths.find(mapId);
        if (it != allPaths.end() && !it->second.empty()) {
            path = &it->second.begin()->second;
        }
    }
    if (!path || path->size() < 2) return false;

    // Track previous index for path resumption
    static size_t previousIndex = 0;

    // Find the closest waypoint
    size_t closest = 0;
    float minDist = std::numeric_limits<float>::max();
    for (size_t i = 0; i < path->size(); ++i) {
        float dx = bot->GetPositionX() - (*path)[i].x;
        float dy = bot->GetPositionY() - (*path)[i].y;
        float dz = bot->GetPositionZ() - (*path)[i].z;
        float dist = dx*dx + dy*dy + dz*dz;
        if (dist < minDist) {
            minDist = dist;
            closest = i;
        }
    }

    size_t index = closest;
    float distToClosest = sqrtf(minDist);

    // Path resumption logic
    float distToPrev = 0.0f;
    if (previousIndex < path->size()) {
        float dx = bot->GetPositionX() - (*path)[previousIndex].x;
        float dy = bot->GetPositionY() - (*path)[previousIndex].y;
        float dz = bot->GetPositionZ() - (*path)[previousIndex].z;
        distToPrev = sqrtf(dx*dx + dy*dy + dz*dz);
    }
    if (distToPrev <= 40.0f) {
        // Resume at closest in [previousIndex, previousIndex+3]
        size_t resumeFloor = previousIndex;
        size_t resumeCeil = std::min(previousIndex + 3, path->size() - 2);
        size_t resumeIndex = resumeFloor;
        float minDistResume = std::numeric_limits<float>::max();
        for (size_t i = resumeFloor; i <= resumeCeil; ++i) {
            float dx = bot->GetPositionX() - (*path)[i].x;
            float dy = bot->GetPositionY() - (*path)[i].y;
            float dz = bot->GetPositionZ() - (*path)[i].z;
            float dist = dx*dx + dy*dy + dz*dz;
            if (dist < minDistResume || (dist == minDistResume && i < resumeIndex)) {
                minDistResume = dist;
                resumeIndex = i;
            }
        }
        index = resumeIndex;
    }
    if (distToClosest < 6.0f && index < path->size() - 1) {
        Group* group = bot->GetGroup();
        float maxDistance = sWorld->getFloatConfig(CONFIG_GROUP_XP_DISTANCE);
        bool allHealersHaveMana = true;
        bool allOnSameMap = true;
        bool allWithinDistance = true;
        if (group) {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
                Player* member = ref->GetSource();
                if (!member || !member->IsAlive()) continue;
                if (member->GetMapId() != bot->GetMapId()) {
                    allOnSameMap = false;
                    break;
                }
                float dist = bot->GetDistance(member);
                if (dist > maxDistance) {
                    allWithinDistance = false;
                    break;
                }
                // Check healer mana
                if (botAI->IsHeal(member)) {
                    uint32 mana = member->GetPower(POWER_MANA);
                    uint32 maxMana = member->GetMaxPower(POWER_MANA);
                    float requiredManaPct = (*path)[index].healer_mana_pct;
                    if (maxMana > 0 && mana < requiredManaPct * maxMana) {
                        allHealersHaveMana = false;
                        break;
                    }
                }
            }
        }
        static std::chrono::steady_clock::time_point lastNotifyTime = std::chrono::steady_clock::now() - std::chrono::seconds(30);
        if (allHealersHaveMana && allOnSameMap && allWithinDistance) {
            uint32_t nextIdx = (*path)[index].next_index;
            if (nextIdx < path->size()) {
                previousIndex = index; // Update previousIndex after successful move
                auto now = std::chrono::steady_clock::now();
                const DungeonWaypoint& wp = (*path)[index];
                // Interact with NPC if interact_type == 1 and NPC with interact_guid is nearby
                if (wp.interact_type == 1 && wp.interact_guid != 0) {
                    float interactDist = 20.0f;
                    std::list<Unit*> npcs;
                    Acore::AnyUnitInObjectRangeCheck npcCheck(bot, interactDist);
                    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> npcSearcher(bot, npcs, npcCheck);
                    Cell::VisitObjects(bot, npcSearcher, interactDist);
                    Creature* foundNpc = nullptr;
                    for (Unit* unit : npcs) {
                        if (unit->ToCreature() && unit->ToCreature()->GetEntry() == wp.interact_guid) {
                            foundNpc = unit->ToCreature();
                            break;
                        }
                    }
                    if (foundNpc) {
                        std::ostringstream dbg;
                        dbg << "[DEBUG] Found NPC with entry " << wp.interact_guid << " at waypoint " << index << ". GUID: " << foundNpc->GetGUID().ToString();
                        botAI->TellMasterNoFacing(dbg.str());
                        GossipHelloAction gossipAction(botAI);
                        int32 menuOption = static_cast<int32>(wp.interact_param); // If interact_param is menu index
                        bool interacted = gossipAction.Execute(foundNpc->GetGUID(), menuOption, true);
                        if (!interacted) {
                            // Try default greeting if menu option failed
                            botAI->TellMasterNoFacing("[DEBUG] No gossip menu options, trying default greeting.");
                            gossipAction.Execute(foundNpc->GetGUID(), -1, true);
                        }
                    }
                }
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastNotifyTime).count() >= 10) {
                    if (wp.tell && !wp.comment.empty()) {
                        botAI->TellMasterNoFacing(wp.comment);
                    }
                    if (wp.pause > 0) {
                        botAI->SetNextCheckDelay(wp.pause);
                    }
                    lastNotifyTime = now;
                }
                index = nextIdx;
            }
        } else {
            // At least one healer mana too low, or not all on same map, or someone too far away
            botAI->SetNextCheckDelay(3000);
        }
    }

    if (index >= path->size()) return false;
    const DungeonWaypoint& wp = (*path)[index];
    bool result = false;
    if (wp.jump) {
        result = JumpTo(mapId, wp.x, wp.y, wp.z);
    } else {
        result = MoveTo(mapId, wp.x, wp.y, wp.z);
    }
    // If next_index equals current index, switch to follow strategy
    if (wp.next_index == index) {
        botAI->TellMasterNoFacing("No next waypoint found, following you instead.");
        botAI->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
    }

    // Engagement logic on reaching each waypoint
    if (!bot->IsInCombat()) {
        float aggroDist = sPlayerbotAIConfig->aggroDistance;
        std::list<Unit*> targets;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck u_check(bot, bot, aggroDist);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
        Cell::VisitObjects(bot, searcher, aggroDist);
        for (Unit* unit : targets) {
            if (AttackersValue::IsPossibleTarget(unit, bot, aggroDist) && bot->IsValidAttackTarget(unit)) {
                // Directly attack the first valid target
                bot->SetSelection(unit->GetGUID());
                botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(unit);
                MeleeAction melee(botAI);
                melee.Execute(event);
                break;
            }
        }
    }
    return result;
}
