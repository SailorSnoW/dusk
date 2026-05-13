#include "dusk/autosplit.h"

#include "dusk/io.hpp"
#include "dusk/livesplit.h"
#include "dusk/main.h"
#include "dusk/settings.h"

#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "d/d_save.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_player.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>

namespace dusk::autosplit {

using json = nlohmann::json;

namespace {

constexpr auto ROUTE_FILENAME = "autosplits.json";
constexpr int  ROUTE_FORMAT_VERSION = 2;

bool linkProcIs(int procId) {
    const auto* link = static_cast<const daAlink_c*>(daPy_getPlayerActorClass());
    return link != nullptr && link->mProcID == procId;
}

bool eventBit(u16 flag) {
    return dComIfGs_isEventBit(flag) != 0;
}

bool itemFirstBit(u8 itemNo) {
    return dComIfGs_isItemFirstBit(itemNo) != 0;
}

bool crystalCollected(u8 idx) {
    return dComIfGs_isCollectCrystal(idx);
}

bool mirrorCollected(u8 idx) {
    return dComIfGs_isCollectMirror(idx);
}

// ---------------------------------------------------------------------------
// Minimal .lss segment-name extractor
//
// LiveSplit .lss is XML. We only need ordered <Name> values from inside the
// top-level <Segments> block. The format is stable enough that a tiny
// hand-rolled scanner avoids pulling in an XML dependency.

std::string xmlUnescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0)  { out += '&';  i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0)   { out += '<';  i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)   { out += '>';  i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { out += '"';  i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { out += '\''; i += 6; continue; }
        }
        out += s[i++];
    }
    return out;
}

std::string extractNameInner(std::string_view block) {
    // block is the content between <Name> and </Name>, possibly wrapped in CDATA.
    constexpr std::string_view cdataOpen  = "<![CDATA[";
    constexpr std::string_view cdataClose = "]]>";

    const auto cdataStart = block.find(cdataOpen);
    if (cdataStart != std::string_view::npos) {
        const auto contentStart = cdataStart + cdataOpen.size();
        const auto contentEnd = block.find(cdataClose, contentStart);
        if (contentEnd != std::string_view::npos) {
            return std::string(block.substr(contentStart, contentEnd - contentStart));
        }
    }
    return xmlUnescape(block);
}

std::vector<std::string> parseLssSegmentNames(std::string_view xml) {
    std::vector<std::string> names;

    constexpr std::string_view segmentsOpen  = "<Segments>";
    constexpr std::string_view segmentsClose = "</Segments>";

    const auto segStart = xml.find(segmentsOpen);
    if (segStart == std::string_view::npos) {
        return names;
    }
    const auto segEnd = xml.find(segmentsClose, segStart);
    if (segEnd == std::string_view::npos) {
        return names;
    }
    auto body = xml.substr(segStart + segmentsOpen.size(),
                           segEnd - segStart - segmentsOpen.size());

    constexpr std::string_view segOpen  = "<Segment>";
    constexpr std::string_view segClose = "</Segment>";
    constexpr std::string_view nameOpen  = "<Name>";
    constexpr std::string_view nameClose = "</Name>";

    size_t i = 0;
    while (true) {
        const auto sStart = body.find(segOpen, i);
        if (sStart == std::string_view::npos) {
            break;
        }
        const auto sEnd = body.find(segClose, sStart);
        if (sEnd == std::string_view::npos) {
            break;
        }
        const auto segBody = body.substr(sStart + segOpen.size(),
                                         sEnd - sStart - segOpen.size());
        const auto nStart = segBody.find(nameOpen);
        if (nStart != std::string_view::npos) {
            const auto nEnd = segBody.find(nameClose, nStart);
            if (nEnd != std::string_view::npos) {
                const auto nameBody = segBody.substr(nStart + nameOpen.size(),
                                                     nEnd - nStart - nameOpen.size());
                names.push_back(extractNameInner(nameBody));
            }
        }
        i = sEnd + segClose.size();
    }

    return names;
}

}  // namespace

const char* categoryLabel(Category c) noexcept {
    switch (c) {
    case Category::Items:    return "Items";
    case Category::Twilight: return "Twilight";
    case Category::Bosses:   return "Bosses";
    case Category::Dungeons: return "Dungeons";
    case Category::Misc:     return "Misc";
    }
    return "Misc";
}

Manager& Manager::get() {
    static Manager s_instance;
    return s_instance;
}

Manager::Manager() {
    buildCatalog();
    buildRoutes();
}

void Manager::buildRoutes() {
    mRoutes = {
        {
            // Comprehensive Any% list: ordered by natural run progression, including events
            // used by variants of Any% (Gorge, No EMS, Glitchless, EMS routes, etc.). Runners
            // pick what matches their split layout.
            "Any%",
            {
                // Ordon / opening
                "item_ordon_sword",
                "item_hylian_shield",
                "story_forest_spirit",

                // Faron
                "twilight_faron",
                "item_lantern",
                "item_slingshot",
                "item_boomerang",
                "skill_1_shield_attack",
                "dungeon_forest",
                "shadow_1",

                // Eldin / Mines
                "twilight_eldin",
                "item_bow",
                "item_iron_boots",
                "dungeon_mines",
                "shadow_2",

                // Lanayru / Lakebed
                "twilight_lanayru",
                "item_master_sword",
                "item_clawshot",
                "dungeon_lakebed",
                "shadow_3",

                // Midna revived / Light Sword
                "story_midna_revived",
                "item_light_sword",

                // Arbiter's
                "item_spinner",
                "dungeon_arbiters",
                "mirror_1",

                // Snowpeak
                "story_king_bulblin_desert",
                "item_ball_chain",
                "dungeon_snowpeak",
                "mirror_2",

                // Temple of Time
                "item_dominion_rod",
                "dungeon_temple_of_time",
                "mirror_3",

                // City in the Sky
                "item_dominion_rod_lv2",
                "item_double_clawshots",
                "dungeon_city_in_sky",
                "mirror_4",
                "story_mirror_complete",

                // Endgame
                "story_midna_true_form",
                "boss_pot_miniboss",
                "dungeon_palace_of_twilight",
                "item_light_arrows",
                "story_castle_barrier",
                "boss_ganondorf",
            },
        },
    };
}

void Manager::buildCatalog() {
    mCatalog = {
        // ---- Items ----
        { "item_ordon_sword",       "Ordon Sword",
          "First-time pickup of the Ordon Sword.",
          Category::Items, []{ return itemFirstBit(dItemNo_SWORD_e); } },
        { "item_hylian_shield",     "Hylian Shield",
          "First-time pickup of the Hylian Shield.",
          Category::Items, []{ return itemFirstBit(dItemNo_HYLIA_SHIELD_e); } },
        { "item_slingshot",         "Slingshot",
          "First-time pickup of the Slingshot.",
          Category::Items, []{ return itemFirstBit(dItemNo_PACHINKO_e); } },
        { "item_lantern",           "Lantern",
          "First-time pickup of the Lantern.",
          Category::Items, []{ return itemFirstBit(dItemNo_KANTERA_e); } },
        { "item_hawkeye",           "Hawkeye",
          "First-time pickup of the Hawkeye.",
          Category::Items, []{ return itemFirstBit(dItemNo_HAWK_EYE_e); } },
        { "item_bomb_bag",          "First Bomb Bag",
          "First-time pickup of a Bomb Bag.",
          Category::Items, []{ return itemFirstBit(dItemNo_BOMB_BAG_LV1_e); } },
        { "item_iron_boots",        "Iron Boots",
          "First-time pickup of the Iron Boots.",
          Category::Items, []{ return itemFirstBit(dItemNo_HVY_BOOTS_e); } },
        { "item_empty_bottle",      "First Empty Bottle",
          "First-time pickup of an Empty Bottle.",
          Category::Items, []{ return itemFirstBit(dItemNo_EMPTY_BOTTLE_e); } },
        { "item_boomerang",         "Gale Boomerang",
          "First-time pickup of the Gale Boomerang.",
          Category::Items, []{ return itemFirstBit(dItemNo_BOOMERANG_e); } },
        { "item_bow",               "Hero's Bow",
          "First-time pickup of the Hero's Bow.",
          Category::Items, []{ return itemFirstBit(dItemNo_BOW_e); } },
        { "item_master_sword",      "Master Sword",
          "Acquired the Master Sword (event flag F_0264).",
          Category::Items, []{ return eventBit(dSv_event_flag_c::F_0264); } },
        { "item_light_sword",       "Master Sword Empowered",
          "Master Sword charged with light by Lanayru.",
          Category::Items, []{ return itemFirstBit(dItemNo_LIGHT_SWORD_e); } },
        { "item_clawshot",          "Clawshot",
          "First-time pickup of the Clawshot (Lakebed Temple).",
          Category::Items, []{ return itemFirstBit(dItemNo_HOOKSHOT_e); } },
        { "item_spinner",           "Spinner",
          "First-time pickup of the Spinner.",
          Category::Items, []{ return itemFirstBit(dItemNo_SPINNER_e); } },
        { "item_ball_chain",        "Ball & Chain",
          "First-time pickup of the Ball & Chain (Snowpeak Ruins).",
          Category::Items, []{ return itemFirstBit(dItemNo_IRONBALL_e); } },
        { "item_dominion_rod",      "Dominion Rod",
          "First-time pickup of the Dominion Rod.",
          Category::Items, []{ return itemFirstBit(dItemNo_COPY_ROD_e); } },
        { "item_dominion_rod_lv2",  "Dominion Rod Restored",
          "Dominion Rod restored by the Sky Cannon owl statues.",
          Category::Items, []{ return itemFirstBit(dItemNo_COPY_ROD_2_e); } },
        { "item_double_clawshots",  "Double Clawshots",
          "First-time pickup of the second Clawshot (City in the Sky).",
          Category::Items, []{ return itemFirstBit(dItemNo_W_HOOKSHOT_e); } },
        { "item_light_arrows",      "Light Arrows",
          "First-time pickup of the Light Arrows.",
          Category::Items, []{ return itemFirstBit(dItemNo_LIGHT_ARROW_e); } },
        { "item_fishing_rod",       "Fishing Rod",
          "First-time pickup of the Fishing Rod.",
          Category::Items, []{ return itemFirstBit(dItemNo_FISHING_ROD_1_e); } },

        // ---- Twilight vessels ----
        { "twilight_faron",         "Faron Twilight Cleared",
          "Received Vessel of Light from Faron spirit (F_0055).",
          Category::Twilight, []{ return eventBit(dSv_event_flag_c::F_0055); } },
        { "twilight_eldin",         "Eldin Twilight Cleared",
          "Received Vessel of Light from Eldin spirit (F_0221).",
          Category::Twilight, []{ return eventBit(dSv_event_flag_c::F_0221); } },
        { "twilight_lanayru",       "Lanayru Twilight Cleared",
          "Received Vessel of Light from Lanayru spirit (F_0615).",
          Category::Twilight, []{ return eventBit(dSv_event_flag_c::F_0615); } },

        // ---- Fused Shadows (Crystal slots 0..2) ----
        { "shadow_1",               "Fused Shadow #1 Collected",
          "First Fused Shadow obtained (typically Forest Temple).",
          Category::Items, []{ return crystalCollected(0); } },
        { "shadow_2",               "Fused Shadow #2 Collected",
          "Second Fused Shadow obtained (typically Goron Mines).",
          Category::Items, []{ return crystalCollected(1); } },
        { "shadow_3",               "Fused Shadow #3 Collected",
          "Third Fused Shadow obtained (typically Lakebed Temple).",
          Category::Items, []{ return crystalCollected(2); } },

        // ---- Mirror Shards (Mirror slots 0..3) ----
        { "mirror_1",               "Mirror Shard #1 Collected",
          "First Mirror Shard obtained (Arbiter's Grounds).",
          Category::Items, []{ return mirrorCollected(0); } },
        { "mirror_2",               "Mirror Shard #2 Collected",
          "Second Mirror Shard obtained (Snowpeak Ruins).",
          Category::Items, []{ return mirrorCollected(1); } },
        { "mirror_3",               "Mirror Shard #3 Collected",
          "Third Mirror Shard obtained (Temple of Time).",
          Category::Items, []{ return mirrorCollected(2); } },
        { "mirror_4",               "Mirror Shard #4 Collected",
          "Fourth Mirror Shard obtained (City in the Sky).",
          Category::Items, []{ return mirrorCollected(3); } },

        // ---- Dungeon clears ----
        { "dungeon_forest",         "Forest Temple Cleared",
          "Forest Temple cleared (Diababa defeated, M_022).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::M_022); } },
        { "dungeon_mines",          "Goron Mines Cleared",
          "Goron Mines cleared (Fyrus defeated, M_031).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::M_031); } },
        { "dungeon_lakebed",        "Lakebed Temple Cleared",
          "Lakebed Temple cleared (Morpheel defeated, M_045).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::M_045); } },
        { "dungeon_arbiters",       "Arbiter's Grounds Cleared",
          "Arbiter's Grounds cleared (Stallord defeated, F_0265).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::F_0265); } },
        { "dungeon_snowpeak",       "Snowpeak Ruins Cleared",
          "Snowpeak Ruins cleared (Blizzeta defeated, F_0266).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::F_0266); } },
        { "dungeon_temple_of_time", "Temple of Time Cleared",
          "Temple of Time cleared (Armogohma defeated, F_0267).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::F_0267); } },
        { "dungeon_city_in_sky",    "City in the Sky Cleared",
          "City in the Sky cleared (Argorok defeated, F_0268).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::F_0268); } },
        { "dungeon_palace_of_twilight", "Palace of Twilight Cleared",
          "Palace of Twilight cleared (Zant defeated, F_0570).",
          Category::Dungeons, []{ return eventBit(dSv_event_flag_c::F_0570); } },

        // ---- Story / mini-boss milestones ----
        { "story_forest_spirit",    "Faron Spirit Revived (Hero's Birth)",
          "Forest spirit cutscene (M_019).",
          Category::Bosses, []{ return eventBit(dSv_event_flag_c::M_019); } },
        { "story_king_bulblin_desert", "King Bulblin Defeated (Desert)",
          "King Bulblin defeated on the desert boar fight (M_057).",
          Category::Bosses, []{ return eventBit(dSv_event_flag_c::M_057); } },
        { "story_midna_revived",    "Midna Revived (Reunion w/ Zelda)",
          "Cutscene where Midna is revived by Zelda (F_0250).",
          Category::Bosses, []{ return eventBit(dSv_event_flag_c::F_0250); } },
        { "story_midna_true_form",  "Midna's True Form Revealed",
          "Cutscene revealing Midna's true form (F_0526).",
          Category::Bosses, []{ return eventBit(dSv_event_flag_c::F_0526); } },
        { "story_mirror_complete",  "Mirror of Twilight Complete",
          "Mirror complete cutscene (F_0354).",
          Category::Bosses, []{ return eventBit(dSv_event_flag_c::F_0354); } },
        { "story_castle_barrier",   "Hyrule Castle Barrier Down",
          "Hyrule Castle barrier disappears, Midna's sacrifice (F_0542).",
          Category::Bosses, []{ return eventBit(dSv_event_flag_c::F_0542); } },
        { "boss_pot_miniboss",      "Phantom Zants Defeated",
          "Palace of Twilight mini-boss defeated (F_0326).",
          Category::Bosses, []{ return eventBit(dSv_event_flag_c::F_0326); } },
        { "boss_ganondorf",         "Ganondorf Defeated",
          "Final blow dealt to Ganondorf.",
          Category::Bosses, []{ return linkProcIs(daAlink_c::PROC_GANON_FINISH); } },

        // ---- Hidden Skills ----
        { "skill_1_shield_attack",   "Hidden Skill 1 — Ending Blow",
          "Obtained 1st hidden skill (F_0338).",
          Category::Misc, []{ return eventBit(dSv_event_flag_c::F_0338); } },
        { "skill_2",                 "Hidden Skill 2",
          "Obtained 2nd hidden skill (F_0339).",
          Category::Misc, []{ return eventBit(dSv_event_flag_c::F_0339); } },
        { "skill_3",                 "Hidden Skill 3",
          "Obtained 3rd hidden skill (F_0340).",
          Category::Misc, []{ return eventBit(dSv_event_flag_c::F_0340); } },
        { "skill_4",                 "Hidden Skill 4",
          "Obtained 4th hidden skill (F_0341).",
          Category::Misc, []{ return eventBit(dSv_event_flag_c::F_0341); } },
        { "skill_5",                 "Hidden Skill 5",
          "Obtained 5th hidden skill (F_0342).",
          Category::Misc, []{ return eventBit(dSv_event_flag_c::F_0342); } },
        { "skill_6",                 "Hidden Skill 6",
          "Obtained 6th hidden skill (F_0343).",
          Category::Misc, []{ return eventBit(dSv_event_flag_c::F_0343); } },
        { "skill_7",                 "Hidden Skill 7",
          "Obtained 7th hidden skill (F_0344).",
          Category::Misc, []{ return eventBit(dSv_event_flag_c::F_0344); } },
    };
}

const SplitEvent* Manager::findEvent(std::string_view id) const {
    for (const auto& ev : mCatalog) {
        if (id == ev.id) {
            return &ev;
        }
    }
    return nullptr;
}

void Manager::loadIfNeeded() {
    if (mLoaded) {
        return;
    }
    mLoaded = true;

    const auto filePath = dusk::ConfigPath / ROUTE_FILENAME;
    if (!std::filesystem::exists(filePath)) {
        return;
    }

    try {
        auto bytes = io::FileStream::ReadAllBytes(filePath);
        auto j = json::parse(bytes);
        if (!j.is_object()) {
            return;
        }

        if (j.contains("lssPath") && j["lssPath"].is_string()) {
            mLssPath = j["lssPath"].get<std::string>();
        }

        if (j.contains("splits") && j["splits"].is_array()) {
            for (const auto& entry : j["splits"]) {
                if (!entry.is_object() || !entry.contains("name")) {
                    continue;
                }
                MappedSplit s;
                s.name = entry["name"].get<std::string>();
                if (entry.contains("eventId") && entry["eventId"].is_string()) {
                    auto id = entry["eventId"].get<std::string>();
                    if (findEvent(id) != nullptr) {
                        s.eventId = std::move(id);
                    }
                }
                mSplits.push_back(std::move(s));
            }
        }
    } catch (const std::exception&) {}
}

void Manager::save() {
    json j;
    j["version"] = ROUTE_FORMAT_VERSION;
    j["lssPath"] = mLssPath;
    auto& arr = j["splits"] = json::array();
    for (const auto& s : mSplits) {
        arr.push_back({{"name", s.name}, {"eventId", s.eventId}});
    }
    try {
        io::FileStream::WriteAllText(dusk::ConfigPath / ROUTE_FILENAME, j.dump(2));
    } catch (const std::exception&) {}
}

void Manager::captureInitialStates() {
    mInitialState.clear();
    for (const auto& ev : mCatalog) {
        bool current = false;
        try {
            current = ev.check();
        } catch (...) {}
        mInitialState[ev.id] = current;
    }
}

void Manager::onSpeedrunStart() {
    loadIfNeeded();
    mNextSplit = 0;
    captureInitialStates();
}

void Manager::onSpeedrunReset() {
    mNextSplit = 0;
    mInitialState.clear();
}

void Manager::tick() {
    loadIfNeeded();

    if (!dusk::IsGameLaunched) {
        return;
    }
    const auto& s = getSettings().game;
    if (!s.speedrunMode.getValue() || !s.liveSplitEnabled.getValue() || !s.autosplitEnabled.getValue()) {
        return;
    }
    if (mInitialState.empty()) {
        return;
    }
    if (!speedrun::isConnected()) {
        return;
    }

    while (mNextSplit < mSplits.size()) {
        const auto& split = mSplits[mNextSplit];
        if (split.eventId.empty()) {
            // Unmapped split: halt the autosplit engine here. Runner must split
            // LiveSplit manually for this segment (or assign an event).
            return;
        }
        const auto* ev = findEvent(split.eventId);
        if (ev == nullptr) {
            // Event no longer in catalog (deprecated id) — treat as unmapped.
            return;
        }

        const bool initial = mInitialState[split.eventId];
        bool now = false;
        try {
            now = ev->check();
        } catch (...) {}

        if (now && !initial) {
            speedrun::split();
            mInitialState[split.eventId] = true;
            ++mNextSplit;
            continue;
        }
        return;
    }
}

ImportResult Manager::importFromLss(const std::string& path) {
    loadIfNeeded();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return ImportResult::FileNotFound;
    }

    std::vector<u8> bytes;
    try {
        bytes = io::FileStream::ReadAllBytes(path.c_str());
    } catch (const std::exception&) {
        return ImportResult::ParseError;
    }

    const std::string_view xml(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    auto names = parseLssSegmentNames(xml);
    if (names.empty()) {
        return ImportResult::NoSegments;
    }

    // Preserve previous event mappings where the (name, position) pair still matches.
    std::vector<MappedSplit> next;
    next.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        MappedSplit s;
        s.name = std::move(names[i]);
        if (i < mSplits.size() && mSplits[i].name == s.name) {
            s.eventId = mSplits[i].eventId;
        }
        next.push_back(std::move(s));
    }

    mSplits  = std::move(next);
    mLssPath = path;
    mNextSplit = 0;
    save();
    return ImportResult::Success;
}

void Manager::setEventForSplit(size_t index, std::string eventId) {
    loadIfNeeded();
    if (index >= mSplits.size()) {
        return;
    }
    if (!eventId.empty() && findEvent(eventId) == nullptr) {
        return;
    }
    mSplits[index].eventId = std::move(eventId);
    save();
}

void Manager::clearSplits() {
    loadIfNeeded();
    mSplits.clear();
    mLssPath.clear();
    mNextSplit = 0;
    save();
}

}  // namespace dusk::autosplit
