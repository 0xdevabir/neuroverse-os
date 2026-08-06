// neuro/ui/scene.hpp
//
// UI compositor skeleton.
//
// Per README §4.9 (NeuroUI):
//   - The compositor is a normal user process that owns a Scene
//     tree and presents it to one or more output devices.
//   - A Scene is a tree of Nodes; each Node carries a transform,
//     a clip rect, and a payload (draw call, image, video frame,
//     text glyph run, sub-scene).
//   - The compositor walks the tree top-down, applies the local
//     transform, and emits a flat draw list to the output.
//
// On the host scaffold we keep the Node types and the Scene
// trait. The actual GPU back-end (Vulkan / Metal / GLES) lands
// alongside the kernel display driver in Phase 1.

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace neuro::ui {

struct Vec2 {
    float x = 0;
    float y = 0;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

struct Affine {
    // 2x3 matrix in row-major order: [a b tx; c d ty].
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    static Affine identity() noexcept { return {}; }
    static Affine translate(Vec2 t) noexcept {
        return Affine{1, 0, 0, 1, t.x, t.y};
    }
};

// One draw call the compositor emits.
struct DrawCmd {
    Rect                clip;
    std::vector<std::byte> payload;  // backend-specific
};

// Node payloads: what kind of work the node represents.
struct TextGlyphs {
    std::string text;
    float       size_px = 14;
};
struct ImageRef {
    std::uint64_t image_id = 0;  // resolved through NeuroPkg
};
struct SubSceneRef {
    std::uint64_t scene_id = 0;
};

// Forward declaration.
class Scene;

// One node in the scene tree.
class Node {
public:
    Node() = default;
    explicit Node(Affine xf) : xf_(xf) {}

    [[nodiscard]] Affine transform() const noexcept { return xf_; }
    void set_transform(Affine x) noexcept { xf_ = x; }

    [[nodiscard]] Rect clip() const noexcept { return clip_; }
    void set_clip(Rect r) noexcept { clip_ = r; }

    // Children. The Scene owns the lifetime.
    void add_child(Node* n) { children_.push_back(n); }
    [[nodiscard]] std::span<Node* const> children() const noexcept {
        return children_;
    }

    // Replace the payload.
    void set_text(TextGlyphs t) { payload_ = std::move(t); }
    void set_image(ImageRef i)   { payload_ = i; }
    void set_sub_scene(SubSceneRef s) { payload_ = s; }

    [[nodiscard]] const std::variant<std::monostate, TextGlyphs,
                                      ImageRef, SubSceneRef>&
    payload() const noexcept { return payload_; }

private:
    Affine                                            xf_    = Affine::identity();
    Rect                                              clip_  = {};
    std::vector<Node*>                                children_;
    std::variant<std::monostate, TextGlyphs,
                 ImageRef, SubSceneRef>               payload_;
};

// A scene is a tree of nodes plus an output target.
class Scene {
public:
    Scene() = default;
    explicit Scene(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    [[nodiscard]] Node* root() noexcept { return &root_; }
    [[nodiscard]] const Node* root() const noexcept { return &root_; }

    // Recursively flatten the tree into a draw list. The kernel
    // compositor hands this to the GPU back-end.
    void flatten(std::vector<DrawCmd>& out) const;

private:
    std::string name_;
    Node        root_;
};

}  // namespace neuro::ui
