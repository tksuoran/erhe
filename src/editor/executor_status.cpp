#include "executor_status.hpp"

#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <imgui/imgui.h>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <utility>

namespace editor {

Background_work_owner::Background_work_owner(
    Executor_status&                        executor_status,
    const char* const                       name,
    std::function<Background_work_counts()> get_counts
)
    : m_executor_status{executor_status}
    , m_name           {name}
    , m_get_counts     {std::move(get_counts)}
{
    m_executor_status.register_owner(*this);
}

Background_work_owner::~Background_work_owner() noexcept
{
    m_executor_status.unregister_owner(*this);
}

auto Background_work_owner::get_name() const -> const char*
{
    return m_name;
}

auto Background_work_owner::get_counts() const -> Background_work_counts
{
    return m_get_counts();
}

auto Executor_status_entry::get_busy_worker_count() const -> std::size_t
{
    // The two reads are not one snapshot: a worker may park between them.
    const std::size_t workers = executor->num_workers();
    const std::size_t waiters = executor->num_waiters();
    return (workers > waiters) ? (workers - waiters) : 0;
}

namespace {

void fill_entry(Executor_status_entry& entry, const char* const role, tf::Executor& executor, const int configured_thread_count)
{
    entry.role                    = role;
    entry.executor                = &executor;
    entry.configured_thread_count = configured_thread_count;
    entry.priority_report = erhe::task::get_worker_priority_report(executor);
    for (const erhe::task::Worker_thread_report& worker : entry.priority_report.workers) {
        if (worker.priority_applied) {
            ++entry.priority_applied_count;
        }
        if (worker.read_back_valid) {
            entry.read_back_values.push_back(worker.read_back_value);
        }
    }
    std::sort(entry.read_back_values.begin(), entry.read_back_values.end());
    entry.read_back_values.erase(std::unique(entry.read_back_values.begin(), entry.read_back_values.end()), entry.read_back_values.end());
    entry.read_back_summary = entry.read_back_values.empty() ? std::string{"-"} : fmt::format("{}", fmt::join(entry.read_back_values, ", "));
}

} // anonymous namespace

Executor_status::Executor_status(
    tf::Executor& foreground,
    const int     foreground_configured_thread_count,
    tf::Executor& background,
    const int     background_configured_thread_count
)
{
    fill_entry(m_executors[c_foreground], "foreground", foreground, foreground_configured_thread_count);
    fill_entry(m_executors[c_background], "background", background, background_configured_thread_count);
}

auto Executor_status::get_executors() const -> const std::array<Executor_status_entry, 2>&
{
    return m_executors;
}

void Executor_status::register_owner(Background_work_owner& owner)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_owners.push_back(&owner);
}

void Executor_status::unregister_owner(Background_work_owner& owner)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    const std::vector<Background_work_owner*>::iterator i = std::find(m_owners.begin(), m_owners.end(), &owner);
    ERHE_VERIFY(i != m_owners.end());
    m_owners.erase(i);
}

void Executor_status::get_background_work(std::vector<Background_work_report>& out) const
{
    out.clear();
    std::lock_guard<std::mutex> lock{m_mutex};
    for (const Background_work_owner* owner : m_owners) {
        out.push_back(Background_work_report{.name = owner->get_name(), .counts = owner->get_counts()});
    }
}

auto Executor_status::is_background_idle() const -> bool
{
    std::lock_guard<std::mutex> lock{m_mutex};
    for (const Background_work_owner* owner : m_owners) {
        const Background_work_counts counts = owner->get_counts();
        if ((counts.queued != 0) || (counts.running != 0) || (counts.ready != 0)) {
            return false;
        }
    }
    return true;
}

void Executor_status::imgui()
{
    if (!ImGui::CollapsingHeader("Workers", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##executors", 7, table_flags)) {
        ImGui::TableSetupColumn("Executor");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Workers");
        ImGui::TableSetupColumn("Priority");
        ImGui::TableSetupColumn("Read back");
        ImGui::TableSetupColumn("Busy");
        ImGui::TableSetupColumn("Graphs");
        ImGui::TableHeadersRow();
        for (const Executor_status_entry& entry : m_executors) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.role);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.priority_report.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%zu", entry.executor->num_workers());
            ImGui::TableNextColumn();
            ImGui::Text(
                "%s (%zu of %zu applied)",
                erhe::task::c_str(entry.priority_report.requested),
                entry.priority_applied_count,
                entry.priority_report.workers.size()
            );
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.read_back_summary.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%zu", entry.get_busy_worker_count());
            ImGui::TableNextColumn(); ImGui::Text("%zu", entry.executor->num_topologies());
        }
        ImGui::EndTable();
    }
    get_background_work(m_ui_reports);
    if (ImGui::BeginTable("##background_work", 4, table_flags)) {
        ImGui::TableSetupColumn("Background work");
        ImGui::TableSetupColumn("Queued");
        ImGui::TableSetupColumn("Running");
        ImGui::TableSetupColumn("Ready");
        ImGui::TableHeadersRow();
        for (const Background_work_report& report : m_ui_reports) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(report.name);
            ImGui::TableNextColumn(); ImGui::Text("%zu", report.counts.queued);
            ImGui::TableNextColumn(); ImGui::Text("%zu", report.counts.running);
            ImGui::TableNextColumn(); ImGui::Text("%zu", report.counts.ready);
        }
        ImGui::EndTable();
    }
}

} // namespace editor
