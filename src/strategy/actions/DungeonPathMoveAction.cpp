#include "AiObjectContext.h"
#include "DungeonPathMoveAction.h"
#include "DungeonWaypointMgr.h"
#include "GenericActions.h"
#include "AttackersValue.h"
#include "GridNotifiers.h"
#include "GossipHelloAction.h"
#include <limits>
#include <chrono>

DungeonPathMoveAction::DungeonPathMoveAction(PlayerbotAI* ai, DungeonWaypointMgr* mgr)
    : MovementAction(ai, "dungeon path move"), waypointMgr(mgr) {}

bool DungeonPathMoveAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    uint32 mapId = bot->GetMapId();

    // Get the first available path for this mapId
    if (!waypointMgr)
    {
        return false;
    }

    const auto& allPaths = waypointMgr->GetAllPaths();
    auto it = allPaths.find(mapId);
    if (it == allPaths.end() || it->second.empty())
    {
        return false;
    }

    const DungeonPath* path = &it->second.begin()->second;
    if (path->size() < 2)
    {
        return false;
    }

    // Find the closest waypoint
    size_t closest = FindClosestWaypoint(path, bot);
    
    // Calculate distance to closest waypoint
    float dx = bot->GetPositionX() - (*path)[closest].x;
    float dy = bot->GetPositionY() - (*path)[closest].y;
    float dz = bot->GetPositionZ() - (*path)[closest].z;
    float distToClosest = sqrtf(dx*dx + dy*dy + dz*dz);

    size_t index = DetermineTargetIndex(path, bot, closest);


    if (distToClosest < WAYPOINT_REACHED_DISTANCE && index < path->size() - 1)
    {
        const DungeonWaypoint& wp = (*path)[index];
        
        if (CheckGroupConditions(bot, wp))
        {
            uint32_t nextIdx = wp.next_index;
            if (nextIdx < path->size())
            {
                previousIndex = index; // Update previousIndex after successful move
                
                HandleWaypointInteraction(wp, bot, index);
                HandleWaypointNotification(wp);
                
                index = nextIdx;
            }
        } 
        else 
        {
            // At least one healer mana too low, or not all on same map, or someone too far away
            botAI->SetNextCheckDelay(WAIT_FOR_GROUP_DELAY_MS);
        }
    }

    if (index >= path->size()) return false;
    const DungeonWaypoint& wp = (*path)[index];
    bool result = false;
    if (wp.jump)
    {
        result = JumpTo(mapId, wp.x, wp.y, wp.z);
    }
    else
    {
        result = MoveTo(mapId, wp.x, wp.y, wp.z);
    }
    // If next_index equals current index, switch to follow strategy
    if (wp.next_index == index)
    {
        botAI->TellMasterNoFacing("No next waypoint found, following you instead.");
        botAI->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
    }

    // Engagement logic on reaching each waypoint
    HandleCombatEngagement(bot, event);
    return result;
}

size_t DungeonPathMoveAction::FindClosestWaypoint(const DungeonPath* path, Player* bot) const
{
    size_t closest = 0;
    float minDist = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < path->size(); ++i)
    {
        float dx = bot->GetPositionX() - (*path)[i].x;
        float dy = bot->GetPositionY() - (*path)[i].y;
        float dz = bot->GetPositionZ() - (*path)[i].z;
        float dist = dx*dx + dy*dy + dz*dz;
        
        if (dist < minDist)
        {
            minDist = dist;
            closest = i;
        }
    }
    
    return closest;
}

size_t DungeonPathMoveAction::DetermineTargetIndex(const DungeonPath* path, Player* bot, size_t closestIndex)
{
    // Path resumption logic
    float distToPrev = 0.0f;
    if (previousIndex < path->size())
    {
        float dx = bot->GetPositionX() - (*path)[previousIndex].x;
        float dy = bot->GetPositionY() - (*path)[previousIndex].y;
        float dz = bot->GetPositionZ() - (*path)[previousIndex].z;
        distToPrev = sqrtf(dx*dx + dy*dy + dz*dz);
    }
    
    if (distToPrev <= RESUME_PATH_DISTANCE && path->size() > 0)
    {
        // Resume at closest in [previousIndex, previousIndex+RESUME_SEARCH_RANGE]
        size_t resumeFloor = previousIndex;
        size_t resumeCeil = std::min(previousIndex + RESUME_SEARCH_RANGE, 
                                   path->size() - 1);
        size_t resumeIndex = resumeFloor;
        float minDistResume = std::numeric_limits<float>::max();
        
        for (size_t i = resumeFloor; i <= resumeCeil; ++i)
        {
            float dx = bot->GetPositionX() - (*path)[i].x;
            float dy = bot->GetPositionY() - (*path)[i].y;
            float dz = bot->GetPositionZ() - (*path)[i].z;
            float dist = dx*dx + dy*dy + dz*dz;
            
            if (dist < minDistResume || (dist == minDistResume && i < resumeIndex))
            {
                minDistResume = dist;
                resumeIndex = i;
            }
        }
        return resumeIndex;
    }
    
    return closestIndex;
}

bool DungeonPathMoveAction::CheckGroupConditions(Player* bot, const DungeonWaypoint& waypoint) const
{
    Group* group = bot->GetGroup();
    if (!group) return true; // No group checks needed if no group
    
    float maxDistance = sWorld->getFloatConfig(CONFIG_GROUP_XP_DISTANCE);
    
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member) continue;
        
        // Check if any group member is dead - if so, wait for resurrection
        if (!member->IsAlive())
        {
            return false;
        }
        
        // Check if all members are on same map
        if (member->GetMapId() != bot->GetMapId())
        {
            return false;
        }
        
        // Check if all members are within distance
        if (bot->GetDistance(member) > maxDistance)
        {
            return false;
        }
        
        // Check healer mana requirements
        if (botAI->IsHeal(member))
        {
            uint32 mana = member->GetPower(POWER_MANA);
            uint32 maxMana = member->GetMaxPower(POWER_MANA);
            
            if (maxMana > 0 && static_cast<float>(mana) < waypoint.healer_mana_pct * static_cast<float>(maxMana))
            {
                return false;
            }
        }
        
        // Check tank health using same threshold as healer mana
        if (botAI->IsTank(member))
        {
            uint32 health = member->GetHealth();
            uint32 maxHealth = member->GetMaxHealth();
            
            if (maxHealth > 0 && static_cast<float>(health) < waypoint.healer_mana_pct * static_cast<float>(maxHealth))
            {
                return false;
            }
        }
    }
    
    return true;
}

void DungeonPathMoveAction::HandleWaypointInteraction(const DungeonWaypoint& waypoint, Player* bot, size_t waypointIndex)
{
    // Check if we have a pending menu interaction to complete
    if (pendingMenuInteractionIndex == waypointIndex)
    {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceFirstInteraction = std::chrono::duration_cast<std::chrono::milliseconds>(now - menuInteractionTime).count();
        
        if (timeSinceFirstInteraction >= 200) // 200ms delay
        {
            // Find the NPC again for menu selection
            std::list<Unit*> npcs;
            Acore::AnyUnitInObjectRangeCheck npcCheck(bot, NPC_INTERACT_DISTANCE);
            Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> npcSearcher(bot, npcs, npcCheck);
            Cell::VisitObjects(bot, npcSearcher, NPC_INTERACT_DISTANCE);
            
            for (Unit* unit : npcs)
            {
                if (unit->ToCreature() && unit->ToCreature()->GetEntry() == waypoint.interact_guid)
                {
                    Creature* foundNpc = unit->ToCreature();
                    GossipHelloAction gossipAction(botAI);
                    gossipAction.Execute(foundNpc->GetGUID(), pendingMenuOption, true);
                    
                    botAI->TellMasterNoFacing("[DEBUG] Selected menu option " + std::to_string(pendingMenuOption));
                    break;
                }
            }
            
            // Clear pending interaction and mark as completed
            pendingMenuInteractionIndex = SIZE_MAX;
            pendingMenuOption = -1;
            lastInteractedIndex = waypointIndex;
        }
        return; // Don't do initial interaction while waiting for menu selection
    }
    
    if (waypoint.interact_type == 1 && waypoint.interact_guid != 0)
    {
        // Check if we've already interacted with this waypoint
        if (lastInteractedIndex == waypointIndex)
        {
            return; // Already interacted with this waypoint
        }
        
        std::list<Unit*> npcs;
        Acore::AnyUnitInObjectRangeCheck npcCheck(bot, NPC_INTERACT_DISTANCE);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> npcSearcher(bot, npcs, npcCheck);
        Cell::VisitObjects(bot, npcSearcher, NPC_INTERACT_DISTANCE);
        
        Creature* foundNpc = nullptr;
        for (Unit* unit : npcs)
        {
            if (unit->ToCreature() && unit->ToCreature()->GetEntry() == waypoint.interact_guid)
            {
                foundNpc = unit->ToCreature();
                break;
            }
        }
        
        if (foundNpc)
        {
            std::ostringstream dbg;
            dbg << "[DEBUG] Found NPC with entry " << waypoint.interact_guid 
                << ". GUID: " << foundNpc->GetGUID().ToString();
            botAI->TellMasterNoFacing(dbg.str());
            
            GossipHelloAction gossipAction(botAI);
            int32 menuOption = static_cast<int32>(waypoint.interact_param);
            
            if (menuOption == -1)
            {
                // Simple greeting only
                gossipAction.Execute(foundNpc->GetGUID(), -1, true);
                // Mark as fully completed
                lastInteractedIndex = waypointIndex;
            }
            else
            {
                // Two-step process: greeting first
                gossipAction.Execute(foundNpc->GetGUID(), -1, true);
                
                // Set up for second interaction
                pendingMenuInteractionIndex = waypointIndex;
                pendingMenuOption = menuOption;
                menuInteractionTime = std::chrono::steady_clock::now();
                
                // Don't mark as completed yet - wait for menu selection
            }
        }
    }
}

void DungeonPathMoveAction::HandleWaypointNotification(const DungeonWaypoint& waypoint)
{
    auto now = std::chrono::steady_clock::now();
    
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastNotifyTime).count() >= NOTIFICATION_COOLDOWN_SECONDS)
    {
        if (waypoint.tell && !waypoint.comment.empty())
        {
            botAI->TellMasterNoFacing(waypoint.comment);
        }
        
        if (waypoint.pause > 0)
        {
            botAI->SetNextCheckDelay(waypoint.pause);
        }
        
        lastNotifyTime = now;
    }
}

void DungeonPathMoveAction::HandleCombatEngagement(Player* bot, Event event)
{
    if (bot->IsInCombat()) return;
    
    float aggroDist = sPlayerbotAIConfig->aggroDistance;
    std::list<Unit*> targets;
    Acore::AnyUnfriendlyUnitInObjectRangeCheck u_check(bot, bot, aggroDist);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, aggroDist);
    
    for (Unit* unit : targets)
    {
        if (AttackersValue::IsPossibleTarget(unit, bot, aggroDist) && bot->IsValidAttackTarget(unit))
        {
            // Directly attack the first valid target
            bot->SetSelection(unit->GetGUID());
            botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(unit);
            MeleeAction melee(botAI);
            melee.Execute(event);
            break;
        }
    }
}
