#include "UndoStack.hpp"

namespace forge::editor {

void UndoStack::push(const std::string& frameName, const forge::serial::ProjectData& state) noexcept {
    // Discard redo history when pushing a new frame
    if (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(frames_.size()) - 1) {
        frames_.erase(frames_.begin() + currentIndex_ + 1, frames_.end());
    }

    // Add new frame
    frames_.push_back({frameName, state});

    // Enforce capacity limit
    if (frames_.size() > kMaxFrames) {
        frames_.pop_front();
    } else {
        ++currentIndex_;
    }
}

std::string UndoStack::undo() noexcept {
    if (!canUndo()) return "";

    --currentIndex_;
    return frames_[currentIndex_].sceneName;
}

std::string UndoStack::redo() noexcept {
    if (!canRedo()) return "";

    ++currentIndex_;
    return frames_[currentIndex_].sceneName;
}

void UndoStack::clear() noexcept {
    frames_.clear();
    currentIndex_ = -1;
}

} // namespace forge::editor
