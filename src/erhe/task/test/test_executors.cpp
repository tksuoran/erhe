#include "erhe_task/executor.hpp"
#include "erhe_task/task.hpp"

#include <gtest/gtest.h>

#include <future>
#include <memory>

namespace {

class Executors_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_foreground = erhe::task::make_executor(
            erhe::task::Executor_create_info{.name = "Test fg", .thread_count = 2, .priority = erhe::task::Worker_priority::normal}
        );
        m_background = erhe::task::make_executor(
            erhe::task::Executor_create_info{.name = "Test bg", .thread_count = 2, .priority = erhe::task::Worker_priority::lowered}
        );
        m_executors = std::make_unique<erhe::task::Executors>(*m_foreground.get(), *m_background.get());
    }

    void TearDown() override
    {
        m_foreground->wait_for_all();
        m_background->wait_for_all();
        m_executors.reset();
        m_background.reset();
        m_foreground.reset();
    }

    // The executor get_inherited() returns on a worker of the given executor.
    auto inherited_on_worker_of(tf::Executor& executor) -> tf::Executor*
    {
        std::promise<tf::Executor*> result;
        std::future<tf::Executor*>  future = result.get_future();
        erhe::task::spawn(executor, [this, &result]() { result.set_value(&m_executors->get_inherited()); });
        return future.get();
    }

    std::unique_ptr<tf::Executor>          m_foreground;
    std::unique_ptr<tf::Executor>          m_background;
    std::unique_ptr<erhe::task::Executors> m_executors;
};

} // anonymous namespace

TEST_F(Executors_test, inherited_is_foreground_on_a_thread_that_is_not_a_worker)
{
    EXPECT_EQ(&m_executors->get_inherited(), m_foreground.get());
}

TEST_F(Executors_test, inherited_is_foreground_on_a_foreground_worker)
{
    EXPECT_EQ(inherited_on_worker_of(*m_foreground.get()), m_foreground.get());
}

TEST_F(Executors_test, inherited_is_background_on_a_background_worker)
{
    EXPECT_EQ(inherited_on_worker_of(*m_background.get()), m_background.get());
}

TEST_F(Executors_test, background_worker_may_spawn_onto_the_background_executor)
{
    std::promise<void> done;
    std::future<void>  future = done.get_future();
    erhe::task::spawn(*m_background.get(), [this, &done]() {
        erhe::task::spawn(m_executors->get_inherited(), [&done]() { done.set_value(); });
    });
    future.get();
}

TEST_F(Executors_test, foreground_worker_may_spawn_onto_the_background_executor)
{
    std::promise<void> done;
    std::future<void>  future = done.get_future();
    erhe::task::spawn(*m_foreground.get(), [this, &done]() {
        erhe::task::spawn(*m_background.get(), [&done]() { done.set_value(); });
    });
    future.get();
}

TEST_F(Executors_test, background_worker_spawn_onto_the_foreground_executor_is_fatal)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            std::promise<void> done;
            std::future<void>  future = done.get_future();
            erhe::task::spawn(*m_background.get(), [this, &done]() {
                erhe::task::spawn(*m_foreground.get(), [&done]() { done.set_value(); });
            });
            future.get();
        },
        ""
    );
}
