// src/ui/scene.cpp
//
// Scene flattener (host scaffold).

#include "neuro/ui/scene.hpp"

namespace neuro::ui {

namespace {

void flatten_node(const Node& n, std::vector<DrawCmd>& out) {
    // Emit a draw command for any payload-bearing node.
    if (n.payload().index() != 0) {
        DrawCmd cmd;
        cmd.clip = n.clip();
        out.push_back(std::move(cmd));
    }
    for (Node* c : n.children()) {
        flatten_node(*c, out);
    }
}

}  // namespace

void Scene::flatten(std::vector<DrawCmd>& out) const {
    out.clear();
    flatten_node(root_, out);
}

}  // namespace neuro::ui