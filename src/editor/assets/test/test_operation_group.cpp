// The undo groups of Operation_stack: the batch group, the live edit and the rule for other parties' operations.

#include "app_context.hpp"
#include "operations/operation.hpp"
#include "operations/live_edit.hpp"
#include "operations/operation_group.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// Appends "+name" at execute and "-name" at undo to a shared log.
class Logging_operation : public editor::Operation
{
public:
    Logging_operation(std::vector<std::string>& log, std::string name)
        : m_log {log}
        , m_name{std::move(name)}
    {
    }

    void execute(editor::App_context&) override { m_log.push_back("+" + m_name); }
    void undo   (editor::App_context&) override { m_log.push_back("-" + m_name); }

private:
    std::vector<std::string>& m_log;
    std::string               m_name;
};

auto add_executed(editor::Operation_group& group, editor::App_context& context, std::vector<std::string>& log, const char* name) -> std::shared_ptr<editor::Operation>
{
    const std::shared_ptr<editor::Operation> operation = std::make_shared<Logging_operation>(log, name);
    operation->execute(context);
    group.add(operation);
    return operation;
}

TEST(operation_group, empty_group_records_nothing)
{
    editor::Operation_group group;
    group.open();
    EXPECT_TRUE(group.is_open());
    EXPECT_EQ(group.close(), nullptr);
    EXPECT_FALSE(group.is_open());
}

TEST(operation_group, single_operation_is_the_entry)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    editor::Operation_group  group;
    group.open();
    const std::shared_ptr<editor::Operation> a = add_executed(group, context, log, "a");
    EXPECT_EQ(group.close(), a);
}

TEST(operation_group, several_operations_are_one_entry_undone_in_reverse)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    editor::Operation_group  group;
    group.open();
    static_cast<void>(add_executed(group, context, log, "a"));
    static_cast<void>(add_executed(group, context, log, "b"));
    const std::shared_ptr<editor::Operation> entry = group.close();
    ASSERT_NE(entry, nullptr);
    entry->undo(context);
    entry->execute(context);
    EXPECT_EQ(log, (std::vector<std::string>{"+a", "+b", "-b", "-a", "+a", "+b"}));
}

TEST(operation_group, abort_undoes_in_reverse_and_records_nothing)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    editor::Operation_group  group;
    group.open();
    static_cast<void>(add_executed(group, context, log, "a"));
    static_cast<void>(add_executed(group, context, log, "b"));
    static_cast<void>(add_executed(group, context, log, "c"));
    group.undo_after(context, 0);
    EXPECT_EQ(group.get_size(), 0u);
    EXPECT_EQ(group.close(), nullptr);
    EXPECT_EQ(log, (std::vector<std::string>{"+a", "+b", "+c", "-c", "-b", "-a"}));
}

TEST(operation_group, undo_after_keeps_the_earlier_operations_and_the_group_open)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    editor::Operation_group  group;
    group.open();
    const std::shared_ptr<editor::Operation> a = add_executed(group, context, log, "a");
    static_cast<void>(add_executed(group, context, log, "b"));
    static_cast<void>(add_executed(group, context, log, "c"));
    group.undo_after(context, 1);
    EXPECT_TRUE(group.is_open());
    EXPECT_EQ(group.get_size(), 1u);
    EXPECT_EQ(log, (std::vector<std::string>{"+a", "+b", "+c", "-c", "-b"}));
    EXPECT_EQ(group.close(), a);
}

TEST(operation_group, a_group_opens_again_empty)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    editor::Operation_group  group;
    group.open();
    static_cast<void>(add_executed(group, context, log, "a"));
    static_cast<void>(group.close());
    group.open();
    EXPECT_EQ(group.get_size(), 0u);
    EXPECT_EQ(group.close(), nullptr);
}

// Retains an item of its own (Operation::collect_item_references()).
class Retaining_operation : public editor::Operation
{
public:
    explicit Retaining_operation(const erhe::Item_base* item) : m_item{item} {}
    void execute(editor::App_context&) override {}
    void undo   (editor::App_context&) override {}
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override { out_items.insert(m_item); }

private:
    const erhe::Item_base* m_item;
};

TEST(live_edit, names_its_reason_while_open)
{
    editor::Live_edit live_edit;
    EXPECT_FALSE(live_edit.is_open());
    EXPECT_EQ(live_edit.get_reason(), nullptr);
    const editor::Live_edit_token token = live_edit.begin("a stroke", {});
    ASSERT_TRUE(live_edit.is_open());
    EXPECT_TRUE(live_edit.is_owner(token));
    EXPECT_EQ(std::string{live_edit.get_reason()}, "a stroke");
    EXPECT_EQ(live_edit.end(), nullptr);
    EXPECT_FALSE(live_edit.is_open());
    EXPECT_EQ(live_edit.get_reason(), nullptr);
}

TEST(live_edit, a_token_names_only_its_own_open_edit)
{
    editor::App_context context{};
    editor::Live_edit   live_edit;
    editor::Live_edit_token first = live_edit.begin("a stroke", {});
    EXPECT_TRUE(live_edit.is_owner(first));
    EXPECT_FALSE(live_edit.is_owner(editor::Live_edit_token{}));
    static_cast<void>(live_edit.abandon(context));
    EXPECT_FALSE(live_edit.is_owner(first)); // an abandoned edit's token is dead
    const editor::Live_edit_token second = live_edit.begin("another stroke", {});
    EXPECT_FALSE(live_edit.is_owner(first)); // and stays dead for the next edit
    EXPECT_TRUE(live_edit.is_owner(second));
    editor::Live_edit_token moved = std::move(first);
    EXPECT_EQ(first.get_serial(), 0u);
    EXPECT_FALSE(live_edit.is_owner(moved));
    static_cast<void>(live_edit.end());
    EXPECT_FALSE(live_edit.is_owner(second));
}

TEST(live_edit, end_gives_one_entry_of_its_operations)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    editor::Live_edit        live_edit;
    static_cast<void>(live_edit.begin("a stroke", {}));
    for (const char* name : {"a", "b"}) {
        const std::shared_ptr<editor::Operation> operation = std::make_shared<Logging_operation>(log, name);
        operation->execute(context);
        live_edit.add(operation);
    }
    const std::shared_ptr<editor::Operation> entry = live_edit.end();
    ASSERT_NE(entry, nullptr);
    entry->undo(context);
    EXPECT_EQ(log, (std::vector<std::string>{"+a", "+b", "-b", "-a"}));
}

TEST(live_edit, cancel_undoes_in_reverse_without_telling_the_owner)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    int                      told = 0;
    editor::Live_edit        live_edit;
    static_cast<void>(live_edit.begin("a stroke", [&told]() { ++told; }));
    for (const char* name : {"a", "b"}) {
        const std::shared_ptr<editor::Operation> operation = std::make_shared<Logging_operation>(log, name);
        operation->execute(context);
        live_edit.add(operation);
    }
    live_edit.cancel(context);
    EXPECT_FALSE(live_edit.is_open());
    EXPECT_EQ(told, 0);
    EXPECT_EQ(log, (std::vector<std::string>{"+a", "+b", "-b", "-a"}));
}

TEST(live_edit, abandon_undoes_closes_and_hands_back_the_owners_callback)
{
    editor::App_context      context{};
    std::vector<std::string> log;
    editor::Live_edit        live_edit;
    int                      told = 0;
    static_cast<void>(live_edit.begin("a stroke", [&told]() { ++told; }));
    const std::shared_ptr<editor::Operation> operation = std::make_shared<Logging_operation>(log, "a");
    operation->execute(context);
    live_edit.add(operation);
    const std::function<void()> on_abandoned = live_edit.abandon(context);
    EXPECT_FALSE(live_edit.is_open());
    EXPECT_EQ(told, 0); // the stack calls it once nothing executes
    ASSERT_TRUE(static_cast<bool>(on_abandoned));
    on_abandoned();
    EXPECT_EQ(told, 1);
    EXPECT_EQ(log, (std::vector<std::string>{"+a", "-a"}));
}

TEST(live_edit, collect_item_references_reaches_its_operations)
{
    const int               items[2]{};
    const erhe::Item_base*  first  = reinterpret_cast<const erhe::Item_base*>(&items[0]);
    const erhe::Item_base*  second = reinterpret_cast<const erhe::Item_base*>(&items[1]);
    editor::Live_edit       live_edit;
    static_cast<void>(live_edit.begin("a stroke", {}));
    live_edit.add(std::make_shared<Retaining_operation>(first));
    live_edit.add(std::make_shared<Retaining_operation>(second));
    std::unordered_set<const erhe::Item_base*> references;
    live_edit.collect_item_references(references);
    EXPECT_EQ(references, (std::unordered_set<const erhe::Item_base*>{first, second}));
    static_cast<void>(live_edit.end());
    references.clear();
    live_edit.collect_item_references(references);
    EXPECT_TRUE(references.empty());
}

TEST(live_edit, execute_now_is_refused_and_queued_operations_wait_during_a_live_edit)
{
    using editor::Operation_admission;
    using editor::Operation_request;
    using editor::Save_hold;
    editor::Live_edit live_edit;
    // No live edit: a save holds both requests (execute_now() defers into the queue), else both run.
    EXPECT_EQ(editor::get_operation_admission(Operation_request::execute_now, Save_hold::none, live_edit), Operation_admission::execute);
    EXPECT_EQ(editor::get_operation_admission(Operation_request::queue,       Save_hold::none, live_edit), Operation_admission::execute);
    EXPECT_EQ(editor::get_operation_admission(Operation_request::execute_now, Save_hold::held, live_edit), Operation_admission::hold);
    EXPECT_EQ(editor::get_operation_admission(Operation_request::queue,       Save_hold::held, live_edit), Operation_admission::hold);
    static_cast<void>(live_edit.begin("a stroke", {}));
    // A live edit: execute_now() of another party is refused, a queued operation (a worker completion too) waits.
    EXPECT_EQ(editor::get_operation_admission(Operation_request::execute_now, Save_hold::none, live_edit), Operation_admission::refuse);
    EXPECT_EQ(editor::get_operation_admission(Operation_request::queue,       Save_hold::none, live_edit), Operation_admission::hold);
    static_cast<void>(live_edit.end());
    EXPECT_EQ(editor::get_operation_admission(Operation_request::execute_now, Save_hold::none, live_edit), Operation_admission::execute);
}

} // anonymous namespace
