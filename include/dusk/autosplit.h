#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dusk::autosplit {

enum class Category : uint8_t {
    Items,
    Twilight,
    Bosses,
    Dungeons,
    Misc,
};

struct SplitEvent {
    const char* id;
    const char* displayName;
    const char* description;
    Category    category;
    std::function<bool()> check;
};

// A "route" is a curated, named list of event ids relevant to a particular run
// category (e.g. "Any%"). Used purely to help the runner browse events that
// matter for their route — orthogonal to the per-event `Category` taxonomy.
struct Route {
    const char* name;
    std::vector<const char*> eventIds;
};

struct MappedSplit {
    std::string name;     // segment name imported from .lss
    std::string eventId;  // empty => unmapped (autosplit halts here)
};

enum class ImportResult {
    Success,
    FileNotFound,
    ParseError,
    NoSegments,
};

class Manager {
public:
    static Manager& get();

    void tick();
    void onSpeedrunStart();
    void onSpeedrunReset();

    const std::vector<SplitEvent>& catalog() const noexcept { return mCatalog; }
    const std::vector<Route>&      routes() const noexcept  { return mRoutes; }
    const SplitEvent* findEvent(std::string_view id) const;

    const std::vector<MappedSplit>& splits() const noexcept { return mSplits; }
    const std::string& lssPath() const noexcept { return mLssPath; }

    ImportResult importFromLss(const std::string& path);
    void setEventForSplit(size_t index, std::string eventId);
    void clearSplits();

    size_t pendingIndex() const noexcept { return mNextSplit; }

private:
    Manager();
    void buildCatalog();
    void buildRoutes();
    void loadIfNeeded();
    void save();
    void captureInitialStates();

    std::vector<SplitEvent>  mCatalog;
    std::vector<Route>       mRoutes;
    std::vector<MappedSplit> mSplits;
    std::string              mLssPath;
    std::unordered_map<std::string, bool> mInitialState;
    size_t mNextSplit = 0;
    bool   mLoaded = false;
};

const char* categoryLabel(Category c) noexcept;

}  // namespace dusk::autosplit
