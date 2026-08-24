/**
 * bridge/ScoreboardApi.cpp — scoreboard operations (ABI v5 §F).
 *
 * Reads and writes go through the native Scoreboard (Level::getScoreboard).
 * Score identities are "fake player" names — the same namespace vanilla
 * /scoreboard uses — so results line up with in-game state.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>

#include "mc/world/level/Level.h"
#include "mc/world/scores/Objective.h"
#include "mc/world/scores/ObjectiveCriteria.h"
#include "mc/world/scores/PlayerScoreSetFunction.h"
#include "mc/world/scores/ScoreInfo.h"
#include "mc/world/scores/Scoreboard.h"
#include "mc/world/scores/ScoreboardId.h"
#include "mc/world/scores/ScoreboardOperationResult.h"

namespace levi_rs::bridge
{
    namespace
    {
        /** Get-or-create the ScoreboardId for a fake-player name. */
        ScoreboardId const& idFor(Scoreboard& board, std::string const& name)
        {
            auto const& existing = board.getScoreboardId(name);
            if (existing.mRawID != ScoreboardId::INVALID().mRawID) return existing;
            return board.createScoreboardId(name);
        }

        bool modifyScore(Scoreboard& board, std::string const& objName, std::string const& who, int value,
                         PlayerScoreSetFunction fn, int* newValue)
        {
            auto* obj = board.getObjective(objName);
            if (!obj) return false;
            auto const& id = idFor(board, who);
            ScoreboardOperationResult result{};
            int v = board.modifyPlayerScore(result, id, *obj, value, fn);
            if (newValue) *newValue = v;
            return result == ScoreboardOperationResult::Success;
        }
    } // namespace

    bool api_scoreboard_op(int32_t op, LeviRsStr a, LeviRsStr b, int64_t n, void* ctx, LeviRsStrSink out)
    {
        auto* level = levelReady();
        if (!level) return false;
        auto& board = level->getScoreboard();
        std::string sa{a};
        std::string sb{b};

        switch (op)
        {
        case LEVI_RS_SB_ADD_OBJECTIVE:
            {
                auto* criteria = board.getCriteria(Scoreboard::DEFAULT_CRITERIA());
                if (!criteria) return false;
                auto* obj = board.addObjective(sa, sb.empty() ? sa : sb, *criteria);
                return obj != nullptr;
            }
        case LEVI_RS_SB_REMOVE_OBJECTIVE:
            {
                auto* obj = board.getObjective(sa);
                if (!obj) return false;
                return board.removeObjective(obj);
            }
        case LEVI_RS_SB_LIST_OBJECTIVES:
            {
                if (!out) return false;
                std::string list = "[";
                for (auto const* obj : board.getObjectives())
                {
                    if (!obj) continue;
                    list += "{name:\"" + snbtEscape(obj->mName.get())
                        + "\",display:\"" + snbtEscape(obj->mDisplayName.get()) + "\"},";
                }
                if (list.back() == ',') list.pop_back();
                list += "]";
                out(ctx, list);
                return true;
            }
        case LEVI_RS_SB_GET_SCORE:
            {
                auto* obj = board.getObjective(sa);
                if (!obj || !out) return false;
                auto const& id = board.getScoreboardId(sb);
                if (id.mRawID == ScoreboardId::INVALID().mRawID) return false;
                auto info = obj->getPlayerScore(id);
                if (!info.mValid) return false;
                out(ctx, std::to_string(info.mValue));
                return true;
            }
        case LEVI_RS_SB_SET_SCORE:
            {
                int nv = 0;
                if (!modifyScore(board, sa, sb, static_cast<int>(n), PlayerScoreSetFunction::Set, &nv)) return false;
                if (out) out(ctx, std::to_string(nv));
                return true;
            }
        case LEVI_RS_SB_ADD_SCORE:
            {
                int nv = 0;
                if (!modifyScore(board, sa, sb, static_cast<int>(n), PlayerScoreSetFunction::Add, &nv)) return false;
                if (out) out(ctx, std::to_string(nv));
                return true;
            }
        case LEVI_RS_SB_REDUCE_SCORE:
            {
                int nv = 0;
                if (!modifyScore(board, sa, sb, static_cast<int>(n), PlayerScoreSetFunction::Subtract, &nv)) return
                    false;
                if (out) out(ctx, std::to_string(nv));
                return true;
            }
        case LEVI_RS_SB_RESET_SCORE:
            {
                auto* obj = board.getObjective(sa);
                if (!obj) return false;
                auto const& id = board.getScoreboardId(sb);
                if (id.mRawID == ScoreboardId::INVALID().mRawID) return false;
                return board.resetPlayerScore(id, *obj);
            }
        case LEVI_RS_SB_SET_DISPLAY:
            // Display slot names are engine strings ("sidebar"/"list"/"belowname");
            // route through /scoreboard so sort-order defaults stay engine-defined.
            return runConsoleCommand("scoreboard objectives setdisplay " + sa + " " + sb);
        case LEVI_RS_SB_CLEAR_DISPLAY:
            return runConsoleCommand("scoreboard objectives setdisplay " + sa);
        default:
            return false;
        }
    }
} // namespace levi_rs::bridge
