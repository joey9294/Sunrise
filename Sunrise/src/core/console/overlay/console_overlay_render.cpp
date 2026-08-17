#include <array>
#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <string_view>

#include "../output/console_output.h"
#include "../parser/console_line_parse.h"
#include "../queue/console_queue.h"
#include "console_builtins.h"
#include "console_completion.h"
#include "console_history.h"
#include "console_overlay.h"

namespace sunrise::core::console::overlay {
namespace {

/** The console covers the top of the viewport, which is where a reader expects one to drop. */
constexpr float kViewportHeightShare = 0.45F;
/** Zero size lets the scrollback child take every row left above the prompt. */
constexpr ImVec2 kAutomaticChildSize{0.0F, 0.0F};
/** 1 scrolls to the bottom edge of the newest line. */
constexpr float kScrollBottom = 1.0F;
/** The prompt is the whole width, so a long line is edited without a horizontal scroll. */
constexpr float kFullWidth = -1.0F;
/** Fixed prompt label, kept out of the input so it cannot be edited away. */
constexpr char kPromptLabel[] = "##console_input";
/** Child region holding the scrollback. */
constexpr char kScrollbackId[] = "##console_scrollback";
/** Window title, hidden by the flags but still the window's identity. */
constexpr char kWindowTitle[] = "Sunrise Console##console_window";

/** The console owns its whole strip, so nothing about it is moved, resized or remembered. */
constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                                          | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                                          | ImGuiWindowFlags_NoSavedSettings
                                          | ImGuiWindowFlags_NoBringToFrontOnFocus;
/** A border separates the lines already answered from the line being typed. */
constexpr ImGuiChildFlags kScrollbackFlags = ImGuiChildFlags_Borders;
/** Enter submits, and the two callbacks carry history and completion. */
constexpr ImGuiInputTextFlags kInputFlags = ImGuiInputTextFlags_EnterReturnsTrue
                                            | ImGuiInputTextFlags_CallbackHistory
                                            | ImGuiInputTextFlags_CallbackCompletion;

/** Echoed lines are dimmer than answers, so the eye finds the answers first. */
constexpr ImVec4 kInputTint{0.62F, 0.62F, 0.66F, 1.0F};
/** Answers keep the theme's own text color. */
constexpr ImVec4 kAnswerTint{0.90F, 0.90F, 0.92F, 1.0F};
/** A refusal is red enough to find while scrolling, without being the only cue. */
constexpr ImVec4 kFailureTint{0.93F, 0.44F, 0.40F, 1.0F};
/** What the console says about itself sits apart from what a module answered. */
constexpr ImVec4 kNoticeTint{0.55F, 0.75F, 0.95F, 1.0F};

/** The line being edited. */
std::array<char, parser::kLineCapacity> g_input{};
History g_history{};
/** Set while the prompt should take focus, which is the frame after the console opens. */
bool g_focusPrompt{true};
/** Set while the scrollback should jump to its newest line. */
bool g_scrollToBottom{true};
/** Visibility from the previous frame, so opening can be told from staying open. */
bool g_wasVisible{};

/** @param kind Line kind to tint. @return Its color. */
[[nodiscard]] const ImVec4& tint_for(output::LineKind kind) noexcept {
    switch (kind) {
    case output::LineKind::input:
        return kInputTint;
    case output::LineKind::failure:
        return kFailureTint;
    case output::LineKind::notice:
        return kNoticeTint;
    case output::LineKind::answer:
        break;
    }
    return kAnswerTint;
}

/** Receives one drained result on the render thread and prints it. */
void on_result(std::uint64_t, const Result& result) noexcept {
    output::write_result(result);
    g_scrollToBottom = true;
}

/** Replaces what is typed with one line, leaving the cursor at its end. */
void replace_input(ImGuiInputTextCallbackData* data, std::string_view line) noexcept {
    data->DeleteChars(0, data->BufTextLen);
    if (!line.empty()) {
        data->InsertChars(0, line.data(), line.data() + line.size());
    }
}

/** Applies one history step to the line being edited. */
void apply_history(ImGuiInputTextCallbackData* data) noexcept {
    std::string_view recalled{};
    const bool moved = data->EventKey == ImGuiKey_UpArrow ? step_back(g_history, recalled)
                                                          : step_forward(g_history, recalled);
    if (moved) {
        replace_input(data, recalled);
    }
}

/**
 * Completes the name being typed as far as every match agrees.
 *
 * Only the first token is completed. The rest of a line is a value, and what a value may be is
 * already stated by the entry's own help rather than guessable from a prefix.
 */
void apply_completion(ImGuiInputTextCallbackData* data) noexcept {
    const std::string_view typed{data->Buf, static_cast<std::size_t>(data->BufTextLen)};
    if (typed.find(' ') != std::string_view::npos) {
        return;
    }

    const Completion found = complete(typed);
    if (found.count == 0) {
        return;
    }
    if (found.shared.size() > typed.size()) {
        replace_input(data, found.shared);
    }
    if (found.count == 1) {
        return;
    }
    // More than one name still fits, so the reader is shown what they are choosing between
    // instead of being left with a prefix that stopped growing for no visible reason.
    for (std::size_t index = 0; index < found.count; ++index) {
        output::write(output::LineKind::notice, found.matches[index]);
    }
    if (found.truncated) {
        output::write(output::LineKind::notice, "...and more. Type another letter to narrow it.");
    }
    g_scrollToBottom = true;
}

/** Routes one Dear ImGui input callback to history or completion. */
int input_callback(ImGuiInputTextCallbackData* data) noexcept {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        apply_history(data);
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        apply_completion(data);
    }
    return 0;
}

/** Echoes one submitted line, reads it, and takes it for the drain. */
void submit_line(std::string_view line) noexcept {
    output::write(output::LineKind::input, line);
    remember(g_history, line);
    g_scrollToBottom = true;

    const parser::Outcome outcome = parser::parse_line(line);
    if (outcome.status != Status::ok) {
        Result rejected{};
        rejected.status = outcome.status;
        // The reader is told which name failed, because a mistyped line is most often a mistyped
        // name and quoting it back is what makes the mistake visible.
        if (outcome.status == Status::unknownName && !outcome.requestedName.empty()) {
            Value named{};
            named.type = Type::text;
            store_text(outcome.requestedName, named.text, named.textLength);
            static_cast<void>(add_row(rejected, "name", named));
        }
        output::write_result(rejected);
        return;
    }
    if (queue::submit(outcome.invocation) == queue::kNoTicket) {
        Result refused{};
        refused.status = Status::refused;
        set_summary(refused, "Too much is already waiting to run.");
        output::write_result(refused);
    }
}

/** Draws every retained line, tinted by what it is. */
void draw_scrollback() noexcept {
    const output::Scrollback view = output::snapshot();
    for (const output::Line& line : view.lines()) {
        ImGui::PushStyleColor(ImGuiCol_Text, tint_for(line.kind));
        ImGui::TextUnformatted(line.text.data(), line.text.data() + line.length);
        ImGui::PopStyleColor();
    }
    if (g_scrollToBottom) {
        ImGui::SetScrollHereY(kScrollBottom);
        g_scrollToBottom = false;
    }
}

} // namespace

/** Publishes the console's own entries and clears its editing state. */
bool initialize() noexcept {
    g_input = {};
    g_history = History{};
    g_focusPrompt = true;
    g_scrollToBottom = true;
    g_wasVisible = false;
    return builtins::initialize();
}

/** Runs whatever is waiting, then draws the console when it is showing. */
bool render(bool visible) noexcept {
    // Draining first means a line submitted last frame has already answered by the time the
    // scrollback below is read, so an answer never waits a frame to appear.
    static_cast<void>(queue::drain(&on_result));

    if (!visible) {
        g_wasVisible = false;
        return false;
    }
    if (!g_wasVisible) {
        // Opening puts the caret in the prompt, so the console is typed into rather than clicked.
        g_focusPrompt = true;
        g_scrollToBottom = true;
        g_wasVisible = true;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size{viewport->WorkSize.x, viewport->WorkSize.y * kViewportHeightShare};
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(size);
    if (!ImGui::Begin(kWindowTitle, nullptr, kWindowFlags)) {
        ImGui::End();
        return true;
    }

    const float promptHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 scrollbackSize{kAutomaticChildSize.x, -promptHeight};
    if (ImGui::BeginChild(kScrollbackId, scrollbackSize, kScrollbackFlags)) {
        draw_scrollback();
    }
    ImGui::EndChild();

    if (g_focusPrompt) {
        ImGui::SetKeyboardFocusHere();
        g_focusPrompt = false;
    }
    ImGui::SetNextItemWidth(kFullWidth);
    if (ImGui::InputText(
            kPromptLabel, g_input.data(), g_input.size(), kInputFlags, &input_callback)) {
        const std::string_view line{g_input.data()};
        if (!line.empty()) {
            submit_line(line);
        }
        g_input = {};
        // Enter gives focus away, so it is taken back or the next line would need a click.
        g_focusPrompt = true;
    }

    ImGui::End();
    return true;
}

/** Removes the console's entries and clears its editing state. */
void shutdown() noexcept {
    builtins::shutdown();
    g_input = {};
    g_history = History{};
    g_wasVisible = false;
}

} // namespace sunrise::core::console::overlay
