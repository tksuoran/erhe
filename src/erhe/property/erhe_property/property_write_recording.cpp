#include "erhe_property/property_write_recording.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <string>

namespace erhe::property {

Property_write_recording::Property_write_recording()
{
    if (s_active != nullptr) {
        ERHE_FATAL("Property_write_recording: a recording is already open on this thread (recordings do not nest)");
    }
    s_active = this;
}

Property_write_recording::~Property_write_recording() noexcept
{
    ERHE_VERIFY(s_active == this);
    s_active = nullptr;
}

auto Property_write_recording::take_records() -> std::vector<Property_write_record>
{
    std::vector<Property_write_record> records = std::move(m_records);
    m_records.clear();
    for (Property_write_record& record : records) {
        // A batch still open on a recorded object has notifications queued
        // that the edit has not delivered yet: their consequences would
        // happen after the records were taken.
        if (record.object->m_batch_depth != 0) {
            ERHE_FATAL(
                "Property_write_recording::take_records: property '%s' was recorded on an object with a Change_batch still open",
                std::string{record.property->get_name()}.c_str()
            );
        }
        record.after = record.object->read_local_state(*record.property);
    }
    return records;
}

auto Property_write_recording::get_refusals() const -> std::span<const Property_write_refusal>
{
    return m_refusals;
}

auto Property_write_recording::get_refusal_count() const -> std::size_t
{
    return m_refusals.size();
}

auto Property_write_recording::is_recorded(const Dependency_object& object, const Dependency_property& property) const -> bool
{
    return std::any_of(
        m_records.begin(), m_records.end(),
        [&object, &property](const Property_write_record& record) {
            return (record.object == &object) && (record.property == &property);
        }
    );
}

void Property_write_recording::add_record(Dependency_object& object, const Dependency_property& property, std::optional<Local_state> before)
{
    m_records.push_back(
        Property_write_record{
            .object   = &object,
            .property = &property,
            .before   = std::move(before),
            .after    = std::nullopt
        }
    );
}

void Property_write_recording::add_refusal(Dependency_object& object, const Dependency_property& property)
{
    m_refusals.push_back(
        Property_write_refusal{
            .object   = &object,
            .property = &property
        }
    );
}

void Property_write_recording::forget_object(const Dependency_object& object)
{
    std::erase_if(m_records,  [&object](const Property_write_record&  record ) { return record.object  == &object; });
    std::erase_if(m_refusals, [&object](const Property_write_refusal& refusal) { return refusal.object == &object; });
}

} // namespace erhe::property
