#pragma once

#include "erhe_item/unique_id.hpp"

#include <string>

namespace editor {

class App_context;

class Operation
{
public:
    virtual ~Operation() noexcept;

    virtual void execute(App_context& context) = 0;
    virtual void undo   (App_context& context) = 0;

    [[nodiscard]] auto        describe  () const -> const std::string&;
    [[nodiscard]] inline auto get_serial() const -> std::size_t { return m_id.get_id(); }

    void set_description(std::string&& description);

private:
    erhe::Unique_id<Operation> m_id{};
    std::string                m_description;
};

}
