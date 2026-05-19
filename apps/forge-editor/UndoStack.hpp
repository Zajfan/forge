#pragma once

#include <forge/serial.hpp>
#include <deque>
#include <string>
#include <memory>

namespace forge::editor {

/// Frame in the undo/redo stack
struct UndoFrame {
    std::string sceneName;      ///< Descriptive frame label (e.g., "Move Entity", "Create Brush")
    forge::serial::ProjectData snapshot;  ///< Full project state
};

/// Simple undo/redo stack with fixed capacity
class UndoStack {
public:
    /// Maximum number of undo frames to retain
    static constexpr std::size_t kMaxFrames = 50;

    /// Push a new frame onto the undo stack (clears redo history)
    void push(const std::string& frameName, const forge::serial::ProjectData& state) noexcept;

    /// Undo to the previous frame; returns the previous snapshot or empty on underflow
    [[nodiscard]] std::string undo() noexcept;

    /// Redo to the next frame; returns the next snapshot or empty on underflow
    [[nodiscard]] std::string redo() noexcept;

    /// Check if undo is available
    [[nodiscard]] bool canUndo() const noexcept { return currentIndex_ > 0; }

    /// Check if redo is available
    [[nodiscard]] bool canRedo() const noexcept { return currentIndex_ < static_cast<int>(frames_.size()) - 1; }

    /// Clear all frames
    void clear() noexcept;

    /// Get the count of undo frames available
    [[nodiscard]] std::size_t undoCount() const noexcept { return currentIndex_; }

    /// Get the count of redo frames available
    [[nodiscard]] std::size_t redoCount() const noexcept { return frames_.size() > static_cast<std::size_t>(currentIndex_) ? frames_.size() - currentIndex_ - 1 : 0; }

private:
    std::deque<UndoFrame> frames_;
    int currentIndex_ = -1;  ///< Index into frames_ of the current state; -1 = empty
};

} // namespace forge::editor
