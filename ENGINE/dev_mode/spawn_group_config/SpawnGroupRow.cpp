#include "SpawnGroupRow.hpp"

#include <nlohmann/json.hpp>

#include <utility>

#include "widgets/CandidateEditorPieGraphWidget.hpp"

namespace {
std::function<std::vector<std::string>()> empty_provider() {
    return []() { return std::vector<std::string>{}; };
}
}

SpawnGroupRow::SpawnGroupRow()
    : area_provider_(empty_provider()),
      candidate_graph_(std::make_unique<CandidateEditorPieGraphWidget>()) {
    update_candidate_graph_from_entry();
}

SpawnGroupRow::SpawnGroupRow(nlohmann::json* entry)
    : entry_(entry),
      area_provider_(empty_provider()),
      candidate_graph_(std::make_unique<CandidateEditorPieGraphWidget>()) {
    update_candidate_graph_from_entry();
}

void SpawnGroupRow::bind(nlohmann::json* entry) {
    entry_ = entry;
    if (!entry_) {
        shadow_entry_ = nlohmann::json::object();
    }
    update_candidate_graph_from_entry();
}

void SpawnGroupRow::set_shadow_entry(const nlohmann::json& entry) {
    shadow_entry_ = entry;
    update_candidate_graph_from_entry();
}

nlohmann::json* SpawnGroupRow::mutable_entry() { return entry_; }

const nlohmann::json* SpawnGroupRow::mutable_entry() const { return entry_; }

const nlohmann::json& SpawnGroupRow::entry_view() const {
    if (entry_) {
        return *entry_;
    }
    return shadow_entry_;
}

std::string SpawnGroupRow::spawn_id() const {
    const auto& entry = entry_view();
    if (entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
        return entry["spawn_id"].get<std::string>();
    }
    return std::string{};
}

void SpawnGroupRow::set_ownership_label(const std::string& label, SDL_Color color) {
    ownership_label_ = label;
    ownership_color_ = color;
}

void SpawnGroupRow::clear_ownership_label() {
    ownership_label_.clear();
    ownership_color_.reset();
}

void SpawnGroupRow::set_area_names_provider(std::function<std::vector<std::string>()> provider) {
    area_provider_ = provider ? std::move(provider) : empty_provider();
}

const std::function<std::vector<std::string>()>& SpawnGroupRow::area_names_provider() const {
    return area_provider_;
}

void SpawnGroupRow::set_stack_key(std::string key) {
    stack_key_ = std::move(key);
}

const std::optional<std::string>& SpawnGroupRow::stack_key() const {
    return stack_key_;
}

void SpawnGroupRow::lock_method_to(std::string method) {
    method_lock_ = std::move(method);
}

const std::optional<std::string>& SpawnGroupRow::method_lock() const {
    return method_lock_;
}

void SpawnGroupRow::clear_method_lock() {
    method_lock_.reset();
}

void SpawnGroupRow::set_quantity_hidden(bool hidden) {
    quantity_hidden_ = hidden;
}

bool SpawnGroupRow::quantity_hidden() const {
    return quantity_hidden_;
}

const std::string& SpawnGroupRow::ownership_label() const {
    return ownership_label_;
}

std::optional<SDL_Color> SpawnGroupRow::ownership_color() const {
    return ownership_color_;
}

CandidateEditorPieGraphWidget* SpawnGroupRow::candidate_editor_widget() {
    return candidate_graph_.get();
}

const CandidateEditorPieGraphWidget* SpawnGroupRow::candidate_editor_widget() const {
    return candidate_graph_.get();
}

void SpawnGroupRow::update_candidate_graph_from_entry() {
    if (!candidate_graph_) {
        candidate_graph_ = std::make_unique<CandidateEditorPieGraphWidget>();
    }
    if (!candidate_graph_) {
        return;
    }
    candidate_graph_->set_candidates_from_json(entry_view());
}

