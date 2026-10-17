// Rendergraph::execute() runs every enabled node's pre-execute hook, in
// execution order, then the pre-execute callback, before the first node
// executes (doc/erhe/rendergraph.md "Pre-execute hook"; doc/plans/culling.md
// phase 6a, which classifies every culling view of the frame there).

#include "gpu_test_fixture.hpp"

#include "erhe_file/file_log.hpp"
#include "erhe_graph/graph_log.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace erhe::rendergraph::test {

namespace {

// Records its hook and execute calls into a shared log.
class Recording_node : public Rendergraph_node
{
public:
    Recording_node(Rendergraph& rendergraph, const char* name, std::vector<std::string>& log)
        : Rendergraph_node{rendergraph, erhe::utility::Debug_label{name}}
        , m_name          {name}
        , m_log           {log}
    {
    }

    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override
    {
        static_cast<void>(command_buffer);
        m_log.push_back("execute " + m_name);
    }

protected:
    void pre_execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override
    {
        static_cast<void>(command_buffer);
        m_log.push_back("pre_execute " + m_name);
    }

private:
    std::string               m_name;
    std::vector<std::string>& m_log;
};

} // anonymous namespace

class Rendergraph_pre_execute_test : public erhe::graphics::test::Gpu_test
{
protected:
    void SetUp() override
    {
        Gpu_test::SetUp();
        // The loggers of the libraries the nodes use, once per process (a
        // second initialize_logging() throws: the names are registered),
        // after the GPU environment created the log sinks. erhe::file's own
        // logger is bootstrapped by hand (doc/testing.md).
        static std::once_flag s_once;
        std::call_once(
            s_once,
            []() {
                if (!erhe::file::log_file) {
                    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
                }
                erhe::property::initialize_logging();
                erhe::item::initialize_logging();
                erhe::graph::initialize_logging();
                erhe::rendergraph::initialize_logging();
            }
        );
    }
};

TEST_F(Rendergraph_pre_execute_test, hooks_run_for_every_enabled_node_before_any_execute_in_node_order)
{
    std::vector<std::string> log;
    Rendergraph rendergraph{device()};
    {
        const std::shared_ptr<Recording_node> node_a = make_rendergraph_node<Recording_node>(rendergraph, "a", log);
        const std::shared_ptr<Recording_node> node_b = make_rendergraph_node<Recording_node>(rendergraph, "b", log);
        const std::shared_ptr<Recording_node> node_c = make_rendergraph_node<Recording_node>(rendergraph, "c", log);
        const std::shared_ptr<Recording_node> node_d = make_rendergraph_node<Recording_node>(rendergraph, "d", log);
        // A node disabled in the frame gets neither call.
        node_c->set_enabled(false);
        rendergraph.set_pre_execute_callback(
            [&log](erhe::graphics::Command_buffer& command_buffer) {
                static_cast<void>(command_buffer);
                log.push_back("callback");
            }
        );

        submit_and_wait(
            [&rendergraph](erhe::graphics::Command_buffer& command_buffer) {
                rendergraph.prepare_frame();
                rendergraph.execute(command_buffer);
            }
        );

        // The execution order is the sorted node list.
        std::vector<std::string> expected;
        const std::vector<Rendergraph_node*>& nodes = rendergraph.get_nodes();
        ASSERT_EQ(nodes.size(), 4u);
        for (const Rendergraph_node* node : nodes) {
            if (node != node_c.get()) {
                expected.push_back("pre_execute " + node->get_name());
            }
        }
        expected.push_back("callback");
        for (const Rendergraph_node* node : nodes) {
            if (node != node_c.get()) {
                expected.push_back("execute " + node->get_name());
            }
        }
        EXPECT_EQ(log, expected);
    }
}

TEST_F(Rendergraph_pre_execute_test, execute_without_callback_runs_hooks_then_executes)
{
    std::vector<std::string> log;
    Rendergraph rendergraph{device()};
    {
        const std::shared_ptr<Recording_node> node_a = make_rendergraph_node<Recording_node>(rendergraph, "a", log);
        const std::shared_ptr<Recording_node> node_b = make_rendergraph_node<Recording_node>(rendergraph, "b", log);

        submit_and_wait(
            [&rendergraph](erhe::graphics::Command_buffer& command_buffer) {
                rendergraph.prepare_frame();
                rendergraph.execute(command_buffer);
            }
        );

        ASSERT_EQ(log.size(), 4u);
        EXPECT_EQ(log[0].rfind("pre_execute ", 0), 0u);
        EXPECT_EQ(log[1].rfind("pre_execute ", 0), 0u);
        EXPECT_EQ(log[2].rfind("execute ", 0), 0u);
        EXPECT_EQ(log[3].rfind("execute ", 0), 0u);
    }
}

} // namespace erhe::rendergraph::test
