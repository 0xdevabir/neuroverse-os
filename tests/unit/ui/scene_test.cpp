// tests/unit/ui/scene_test.cpp
//
// Tests for the NeuroUI scene graph. Covers:
//   - Scene::flatten() emits one DrawCmd per payload-bearing node
//   - empty subtrees are skipped
//   - children are walked in insertion order
//   - payload variants (TextGlyphs / ImageRef / SubSceneRef)
//   - Affine / Rect math helpers

#include "neuro/ui/scene.hpp"

#include <cstdint>
#include <string>
#include <variant>

#include "../../test_framework.hpp"

using neuro::ui::Affine;
using neuro::ui::DrawCmd;
using neuro::ui::ImageRef;
using neuro::ui::Node;
using neuro::ui::Rect;
using neuro::ui::Scene;
using neuro::ui::SubSceneRef;
using neuro::ui::TextGlyphs;
using neuro::ui::Vec2;

namespace {

// Check the std::variant holds a particular alternative by index.
template <std::size_t I, typename V>
bool holds(const V& v) {
    return v.index() == I;
}

}  // namespace

// ---- 1. Empty scene flattens to nothing -------------------------------

TEST(scene, empty_scene_flattens_to_nothing) {
    Scene s("empty");
    std::vector<DrawCmd> out;
    s.flatten(out);
    EXPECT_EQ(0u, out.size());
}

// ---- 2. Root with text emits one DrawCmd ------------------------------

TEST(scene, root_with_text_emits_one_cmd) {
    Scene s("hello");
    Node* root = s.root();
    root->set_text(TextGlyphs{"Hello", 16.0f});
    root->set_clip(Rect{0, 0, 100, 50});
    std::vector<DrawCmd> out;
    s.flatten(out);
    EXPECT_EQ(1u, out.size());
    EXPECT_EQ(0,   out[0].clip.x);
    EXPECT_EQ(0,   out[0].clip.y);
    EXPECT_EQ(100, out[0].clip.w);
    EXPECT_EQ(50,  out[0].clip.h);
}

// ---- 3. Payload-less branches are skipped ----------------------------

TEST(scene, payload_less_branches_skipped) {
    Scene s("skipped");
    Node* a = s.root();
    Node* b = new Node();
    Node* c = new Node();
    c->set_text(TextGlyphs{"deep", 12.0f});
    b->add_child(c);
    a->add_child(b);
    std::vector<DrawCmd> out;
    s.flatten(out);
    // Only `c` (the leaf with text) emits; `a` and `b` are skipped.
    EXPECT_EQ(1u, out.size());
    // Note: not deleting b/c — host test, no leak check.
}

// ---- 4. Children walked in insertion order ---------------------------

TEST(scene, children_walked_in_insertion_order) {
    Scene s("order");
    Node* root = s.root();
    Node* a = new Node(); a->set_text(TextGlyphs{"a"});
    Node* b = new Node(); b->set_text(TextGlyphs{"b"});
    Node* c = new Node(); c->set_text(TextGlyphs{"c"});
    root->add_child(a);
    root->add_child(b);
    root->add_child(c);
    std::vector<DrawCmd> out;
    s.flatten(out);
    // 3 children each with text → 3 DrawCmds. We can't read the
    // text back out of the cmd (payload is opaque bytes on host),
    // but the count + ordering is verifiable via clip mutation.
    a->set_clip(Rect{1, 0, 0, 0});
    b->set_clip(Rect{2, 0, 0, 0});
    c->set_clip(Rect{3, 0, 0, 0});
    s.flatten(out);
    EXPECT_EQ(3u, out.size());
    EXPECT_EQ(1, out[0].clip.x);
    EXPECT_EQ(2, out[1].clip.x);
    EXPECT_EQ(3, out[2].clip.x);
}

// ---- 5. Payload variants are accepted -------------------------------

TEST(scene, payload_variants_accepted) {
    Node n;
    n.set_text(TextGlyphs{"hi"});
    EXPECT_TRUE(holds<1>(n.payload()));

    n.set_image(ImageRef{42});
    EXPECT_TRUE(holds<2>(n.payload()));

    n.set_sub_scene(SubSceneRef{7});
    EXPECT_TRUE(holds<3>(n.payload()));
}

// ---- 6. Affine helpers ------------------------------------------------

TEST(scene, affine_identity_is_default) {
    Affine a = Affine::identity();
    EXPECT_EQ(1, a.a);
    EXPECT_EQ(1, a.d);
    EXPECT_EQ(0, a.b);
    EXPECT_EQ(0, a.c);
    EXPECT_EQ(0, a.tx);
    EXPECT_EQ(0, a.ty);
}

TEST(scene, affine_translate_sets_translation) {
    Affine a = Affine::translate(Vec2{10, 20});
    EXPECT_EQ(10, a.tx);
    EXPECT_EQ(20, a.ty);
    EXPECT_EQ(1, a.a);
    EXPECT_EQ(1, a.d);
}

TEST(scene, node_transform_round_trip) {
    Node n;
    n.set_transform(Affine::translate(Vec2{5, 7}));
    Affine a = n.transform();
    EXPECT_EQ(5, a.tx);
    EXPECT_EQ(7, a.ty);
}

// ---- 7. Rect defaults -------------------------------------------------

TEST(scene, rect_defaults_to_zero) {
    Rect r;
    EXPECT_EQ(0, r.x);
    EXPECT_EQ(0, r.y);
    EXPECT_EQ(0, r.w);
    EXPECT_EQ(0, r.h);
}

TEST(scene, vec2_defaults_to_zero) {
    Vec2 v;
    EXPECT_EQ(0, v.x);
    EXPECT_EQ(0, v.y);
}

// ---- 8. Scene name round-trip -----------------------------------------

TEST(scene, scene_name_round_trip) {
    Scene s("my_window");
    EXPECT_EQ("my_window", s.name());
}

RUN_ALL_TESTS()