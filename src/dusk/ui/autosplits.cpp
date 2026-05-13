#include "autosplits.hpp"

#include "Z2AudioLib/Z2SeMgr.h"
#include "m_Do/m_Do_audio.h"

#include "aurora/lib/window.hpp"
#include "dusk/file_select.hpp"
#include "fmt/format.h"
#include "pane.hpp"
#include "ui.hpp"

#include <chrono>

namespace dusk::ui {
namespace {

using autosplit::ImportResult;
using autosplit::Manager;
using autosplit::MappedSplit;
using autosplit::SplitEvent;

constexpr SDL_DialogFileFilter kLssFilters[] = {
    {"LiveSplit splits", "lss"},
    {"All files", "*"},
};

constexpr const char* kClearMappingId = "__clear__";

// Drag/click state shared between the two panes.
int          s_focusedSplitIndex = -1;
std::string  s_pendingDragEvent;

void on_lss_picked(void* /*userdata*/, const char* path, const char* error) {
    if (path == nullptr) {
        if (error != nullptr) {
            push_toast({.title = "Import canceled", .content = error,
                .duration = std::chrono::seconds(3)});
        }
        return;
    }
    const auto result = Manager::get().importFromLss(path);
    switch (result) {
    case ImportResult::Success:
        push_toast({.title = "Splits imported",
            .content = fmt::format("{} splits loaded", Manager::get().splits().size()),
            .duration = std::chrono::seconds(3)});
        break;
    case ImportResult::FileNotFound:
        push_toast({.title = "Import failed", .content = "File not found",
            .duration = std::chrono::seconds(3)});
        break;
    case ImportResult::ParseError:
        push_toast({.title = "Import failed", .content = "Could not read the file",
            .duration = std::chrono::seconds(3)});
        break;
    case ImportResult::NoSegments:
        push_toast({.title = "Import failed",
            .content = "No <Segments> block found in this .lss file",
            .duration = std::chrono::seconds(3)});
        break;
    }
}

void open_lss_picker() {
    const auto& current = Manager::get().lssPath();
    ShowFileSelect(&on_lss_picked, nullptr, aurora::window::get_sdl_window(),
        kLssFilters, static_cast<int>(std::size(kLssFilters)),
        current.empty() ? nullptr : current.c_str(), false);
}

void apply_event_to_split(size_t index, std::string eventId) {
    if (eventId == kClearMappingId) {
        Manager::get().setEventForSplit(index, "");
    } else {
        Manager::get().setEventForSplit(index, std::move(eventId));
    }
}

Rml::String split_button_label(size_t index, const MappedSplit& split, const SplitEvent* ev) {
    const auto eventLabel = ev != nullptr ? Rml::String(ev->displayName) : Rml::String("(unmapped)");
    return fmt::format("{:02}. {}  -  {}", index + 1, split.name, eventLabel);
}

void add_event_button(Pane& pane, const SplitEvent& ev) {
    auto& btn = pane.add_button(Rml::String(ev.displayName));
    btn.root()->SetProperty(Rml::PropertyId::Drag,
        Rml::Property(Rml::Style::Drag::Drag));
    const std::string id(ev.id);
    btn.listen(Rml::EventId::Dragstart,
        [id](Rml::Event&) { s_pendingDragEvent = id; });
    btn.listen(Rml::EventId::Dragend,
        [](Rml::Event&) { s_pendingDragEvent.clear(); });
    btn.on_pressed([id] {
        if (s_focusedSplitIndex < 0) {
            push_toast({.title = "No split selected",
                .content = "Focus a split on the left first.",
                .duration = std::chrono::seconds(2)});
            return;
        }
        mDoAud_seStartMenu(kSoundClick);
        apply_event_to_split(static_cast<size_t>(s_focusedSplitIndex), id);
    });
}

void populate_catalog_pane(Pane& pane) {
    pane.clear();
    auto& mgr = Manager::get();

    if (mgr.splits().empty()) {
        pane.add_section("Events");
        pane.add_text("Import a .lss file from the left panel first."
                      " Once splits are loaded you can drag any event from this catalog onto a"
                      " split to map it, or focus a split and click an event here.");
        pane.finalize();
        return;
    }

    pane.add_section("How to map");
    pane.add_text("Drag an event onto a split, or focus a split (press Right) then click an event.");

    pane.add_section("Clear");
    auto& clearBtn = pane.add_button("(Unmapped — halt at this split)");
    clearBtn.root()->SetProperty(Rml::PropertyId::Drag,
        Rml::Property(Rml::Style::Drag::Drag));
    clearBtn.listen(Rml::EventId::Dragstart,
        [](Rml::Event&) { s_pendingDragEvent = kClearMappingId; });
    clearBtn.listen(Rml::EventId::Dragend,
        [](Rml::Event&) { s_pendingDragEvent.clear(); });
    clearBtn.on_pressed([] {
        if (s_focusedSplitIndex < 0) {
            push_toast({.title = "No split selected",
                .content = "Focus a split on the left first.",
                .duration = std::chrono::seconds(2)});
            return;
        }
        mDoAud_seStartMenu(kSoundClick);
        apply_event_to_split(static_cast<size_t>(s_focusedSplitIndex), kClearMappingId);
    });

    for (const auto& route : mgr.routes()) {
        pane.add_section(fmt::format("Route · {}", route.name));
        for (const char* id : route.eventIds) {
            const auto* ev = mgr.findEvent(id);
            if (ev != nullptr) {
                add_event_button(pane, *ev);
            }
        }
    }

    pane.finalize();
}

void populate_left(Pane& leftPane, Pane& rightPane) {
    leftPane.clear();
    auto& mgr = Manager::get();

    s_focusedSplitIndex = -1;
    s_pendingDragEvent.clear();

    if (mgr.splits().empty()) {
        leftPane.add_section("Import");
        leftPane.add_button("Import Splits from LiveSplit...").on_pressed([] {
            mDoAud_seStartMenu(kSoundClick);
            open_lss_picker();
        });
        leftPane.add_section("Help");
        leftPane.add_text(
            "Pick a LiveSplit (.lss) file. Each segment becomes a slot you map to a game event."
            " The autosplit engine walks slots in order: when the mapped event fires, Dusklight"
            " sends 'split' to LiveSplit. Slots without a mapping halt the autosplit until"
            " the runner splits LiveSplit manually.");
        leftPane.finalize();
        return;
    }

    leftPane.add_section(fmt::format("Splits ({})", mgr.splits().size()));

    for (size_t i = 0; i < mgr.splits().size(); ++i) {
        const auto& split = mgr.splits()[i];
        const auto* ev = split.eventId.empty() ? nullptr
            : mgr.findEvent(split.eventId);
        auto& button = leftPane.add_select_button({
            .key = split_button_label(i, split, ev),
            .submit = false,
        });
        button.root()->SetProperty(Rml::PropertyId::Drag,
            Rml::Property(Rml::Style::Drag::DragDrop));
        button.listen(Rml::EventId::Focus,
            [i](Rml::Event&) { s_focusedSplitIndex = static_cast<int>(i); });
        button.listen(Rml::EventId::Dragdrop,
            [i](Rml::Event&) {
                if (s_pendingDragEvent.empty()) {
                    return;
                }
                auto id = std::move(s_pendingDragEvent);
                s_pendingDragEvent.clear();
                mDoAud_seStartMenu(kSoundClick);
                apply_event_to_split(i, std::move(id));
            });
    }

    leftPane.add_section("Actions");
    leftPane.add_button("Re-import from LiveSplit...").on_pressed([] {
        mDoAud_seStartMenu(kSoundClick);
        open_lss_picker();
    });
    leftPane.add_button("Clear Splits").on_pressed([] {
        mDoAud_seStartMenu(kSoundClick);
        Manager::get().clearSplits();
    });

    leftPane.finalize();
}

}  // namespace

AutosplitsWindow::AutosplitsWindow() {
    snapshotState();
    add_tab("Autosplits", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);
        populate_left(leftPane, rightPane);
        populate_catalog_pane(rightPane);
    });
}

void AutosplitsWindow::snapshotState() {
    mSnapshot = Manager::get().splits();
    mSnapshotPath = Manager::get().lssPath();
}

void AutosplitsWindow::update() {
    const auto& current = Manager::get().splits();
    bool dirty = current.size() != mSnapshot.size() ||
                 Manager::get().lssPath() != mSnapshotPath;
    if (!dirty) {
        for (size_t i = 0; i < current.size(); ++i) {
            if (current[i].name != mSnapshot[i].name ||
                current[i].eventId != mSnapshot[i].eventId) {
                dirty = true;
                break;
            }
        }
    }
    if (dirty) {
        snapshotState();
        refresh_active_tab();
    }
    Window::update();
}

}  // namespace dusk::ui
