#pragma once

#include "erhe_property/expression.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace erhe::property {

class Dependency_object;
class Dependency_property;

// One (object, property) whose local layer was written while a
// Property_write_recording was open: the local layer before the first
// write and after the last one (nullopt: no local value).
class Property_write_record
{
public:
    Dependency_object*         object  {nullptr};
    const Dependency_property* property{nullptr};
    std::optional<Local_state> before  {};
    std::optional<Local_state> after   {};
};

// A local-layer write that a gate refused: read-only, sealed, rejected by
// the property's validate or the bridge validate, a computed property
// without a setter, or an expression that does not compile or would cycle.
class Property_write_refusal
{
public:
    Dependency_object*         object  {nullptr};
    const Dependency_property* property{nullptr};
};

// Collects the local-layer writes made on the current thread while it is
// alive (doc/erhe/property.md "Write recording"). At most one recording is
// open per thread; opening a second one is a usage error (fatal). An object
// destroyed while the recording is open leaves no record or refusal behind.
class Property_write_recording
{
public:
    Property_write_recording();
    Property_write_recording(const Property_write_recording&) = delete;
    Property_write_recording& operator=(const Property_write_recording&) = delete;
    Property_write_recording(Property_write_recording&&) = delete;
    Property_write_recording& operator=(Property_write_recording&&) = delete;
    ~Property_write_recording() noexcept;

    // The records gathered so far, in first-write order, each with its
    // `after` read now; the recording is empty afterwards and keeps
    // recording. A recorded object with a Change_batch still open is a
    // usage error (fatal).
    [[nodiscard]] auto take_records() -> std::vector<Property_write_record>;

    // The refused writes, in the order they were refused.
    [[nodiscard]] auto get_refusals    () const -> std::span<const Property_write_refusal>;
    [[nodiscard]] auto get_refusal_count() const -> std::size_t;

    // The recording open on the current thread, or nullptr.
    [[nodiscard]] static auto get_active() -> Property_write_recording* { return s_active; }

private:
    friend class Dependency_object;

    // Called by Dependency_object's local-layer write paths: a write that
    // passed its gates adds its record (unless the pair is recorded
    // already) before it notifies, so a write made by a changed callback
    // comes after the write that triggered it; a refused write adds a
    // refusal.
    [[nodiscard]] auto is_recorded     (const Dependency_object& object, const Dependency_property& property) const -> bool;
    void               add_record      (Dependency_object& object, const Dependency_property& property, std::optional<Local_state> before);
    void               add_refusal     (Dependency_object& object, const Dependency_property& property);
    // Drops the records and refusals of an object being destroyed.
    void               forget_object   (const Dependency_object& object);

    std::vector<Property_write_record>  m_records;
    std::vector<Property_write_refusal> m_refusals;

    static inline constinit thread_local Property_write_recording* s_active{nullptr};
};

} // namespace erhe::property
