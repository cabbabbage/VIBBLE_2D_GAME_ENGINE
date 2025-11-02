#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Assets;
class Asset;
class Input;
struct SDL_Renderer;
class DMButton;

namespace animation_editor {
class AnimationDocument;
class PreviewProvider;
class AnimationEditorWindow;
}

// Lightweight in-world Frame Editor session.
// Non-modal; anchors panels near the target asset and draws gizmos in world space.
class FrameEditorSession {
public:
    enum class Mode { Movement, Children, Attacking };

    FrameEditorSession();
    ~FrameEditorSession();

    void begin(Assets* assets,
               Asset* asset,
               std::shared_ptr<animation_editor::AnimationDocument> document,
               std::shared_ptr<animation_editor::PreviewProvider> preview,
               const std::string& animation_id,
               animation_editor::AnimationEditorWindow* host_to_toggle,
               std::function<void()> on_end_callback = {});
    void end();

    bool is_active() const { return active_; }

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    // External helpers
    void set_grid_overlay_enabled_transient(bool enabled);

private:
    struct MovementFrame { float dx = 0.0f; float dy = 0.0f; bool resort_z = false; };

    // State wiring
    Assets* assets_ = nullptr;
    Asset* target_ = nullptr;
    std::shared_ptr<animation_editor::AnimationDocument> document_;
    std::shared_ptr<animation_editor::PreviewProvider> preview_;
    animation_editor::AnimationEditorWindow* host_ = nullptr; // to re-open on Back
    std::function<void()> on_end_{};

    // Session state
    bool active_ = false;
    std::string animation_id_;
    int selected_index_ = 0;
    Mode mode_ = Mode::Movement;
    bool show_animation_ = true;

    // Camera + overlay snapshot
    bool prev_realism_enabled_ = true;
    bool prev_parallax_enabled_ = true;
    bool prev_grid_overlay_enabled_ = false;
    bool prev_asset_hidden_ = false;

    // Computed path data (relative positions, anchored at bottom-middle)
    std::vector<MovementFrame> frames_;
    std::vector<SDL_FPoint> rel_positions_;

    // UI widgets
    std::unique_ptr<DMButton> btn_back_;
    std::unique_ptr<DMButton> btn_movement_;
    std::unique_ptr<DMButton> btn_children_;
    std::unique_ptr<DMButton> btn_attacking_;
    std::unique_ptr<DMButton> btn_prev_;
    std::unique_ptr<DMButton> btn_next_;
    std::unique_ptr<DMButton> btn_smooth_;
    std::unique_ptr<DMButton> btn_show_anim_;

    // UI layout (computed each frame)
    mutable SDL_Rect directory_rect_{0,0,0,0};
    mutable SDL_Rect nav_rect_{0,0,0,0};
    mutable std::vector<SDL_Rect> thumb_rects_;

private:
    void ensure_widgets();
    void rebuild_layout() const;
    void apply_frame_move_from_base(int index, SDL_FPoint desired_rel, const std::vector<SDL_FPoint>& base_rel);
    void rebuild_rel_positions();
    void persist_changes();
    void smooth_frames();
    void select_frame(int index);
    void update_asset_preview_frame() const;
    static MovementFrame clamp_frame(const MovementFrame& in);
    static std::vector<MovementFrame> parse_movement_frames_json(const std::string& payload_json);
};
