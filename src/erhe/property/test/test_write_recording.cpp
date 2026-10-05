// Property_write_recording (doc/erhe/property.md "Write recording"): the
// local-layer writes made on the current thread while a recording is open,
// one record per (object, property) with the local state before the first
// write and after the last.

#include "test_object.hpp"
#include "erhe_property/property_write_recording.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

auto type_wr() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_write_recording"); return id; }

class Recording_object : public Test_object
{
public:
    Recording_object() : Test_object{type_wr()} {}
    std::string name{"first"};
    float       stored{0.0f};
};

const Property<float> wr_float  = Property<float>::register_property("wr_float",  type_wr(), Property_metadata{.default_value = 1.0f});
const Property<float> wr_source = Property<float>::register_property("wr_source", type_wr(), Property_metadata{.default_value = 3.0f});
const Property<float> wr_inh    = Property<float>::register_property("wr_inh",    type_wr(), Property_metadata{.default_value = 0.0f, .inherits = true});
const Property<float> wr_validated = Property<float>::register_property(
    "wr_validated", type_wr(), Property_metadata{.default_value = 0.5f},
    [](const Property_value& v) { const float f = std::get<float>(v); return (f >= 0.0f) && (f <= 1.0f); }
);

// Bridged (D18), with a bridge validate that refuses an empty name.
const Property<std::string> wr_name = Property<std::string>::register_property(
    "wr_name", type_wr(),
    Property_metadata{
        .bridge = Property_bridge{
            .get = [](const Dependency_object& o) -> Property_value { return static_cast<const Recording_object&>(o).name; },
            .set = [](Dependency_object& o, const Property_value& v) { static_cast<Recording_object&>(o).name = std::get<std::string>(v); },
            .validate = [](const Dependency_object&, const Property_value& v, std::string& out_error) -> bool {
                if (std::get<std::string>(v).empty()) {
                    out_error = "empty name";
                    return false;
                }
                return true;
            }
        }
    }
);

// Bridged with a metadata default: a clear writes the default.
const Property<float> wr_stored = Property<float>::register_property(
    "wr_stored", type_wr(),
    Property_metadata{
        .default_value = 9.0f,
        .bridge = Property_bridge{
            .get = [](const Dependency_object& o) -> Property_value { return static_cast<const Recording_object&>(o).stored; },
            .set = [](Dependency_object& o, const Property_value& v) { static_cast<Recording_object&>(o).stored = std::get<float>(v); }
        }
    }
);

// Owner-maintained read-only state, written through its key.
const Property_key<float> wr_read_only = Property_key<float>::register_read_only("wr_read_only", type_wr(), Property_metadata{.default_value = 0.0f});

// Computed without a setter.
const Property<float> wr_computed = Property<float>::register_computed(
    "wr_computed", type_wr(),
    [](const Dependency_object& o) -> Property_value { return o.get_value(wr_source) + 1.0f; }
);

// Writable computed (D26): twice wr_target; its setter writes wr_target.
const Property<float> wr_target = Property<float>::register_property("wr_target", type_wr());
const Property<float> wr_double = Property<float>::register_computed(
    "wr_double", type_wr(),
    [](const Dependency_object& o) -> Property_value { return o.get_value(wr_target) * 2.0f; },
    [](Dependency_object& o, const Property_value& value) { o.set_value(wr_target, std::get<float>(value) * 0.5f); },
    wr_target.get()
);

// A changed callback that writes another local layer: wr_follower follows
// wr_leader with an offset.
const Property<float> wr_follower = Property<float>::register_property("wr_follower", type_wr());
const Property<float> wr_leader   = Property<float>::register_property(
    "wr_leader", type_wr(),
    Property_metadata{
        .property_changed = [](Dependency_object& o, const Property_changed_args& args) {
            o.set_value(wr_follower, std::get<float>(args.new_value) + 100.0f);
        }
    }
);

auto local_float(const std::optional<Local_state>& state) -> float
{
    EXPECT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<Property_value>(state.value()));
    return std::get<float>(std::get<Property_value>(state.value()));
}

} // anonymous namespace

TEST(Property_write_recording, no_recording_no_records)
{
    Recording_object o;
    EXPECT_EQ(Property_write_recording::get_active(), nullptr);
    o.set_value(wr_float, 2.0f);
    o.clear_value(wr_source);
    EXPECT_TRUE(o.set_expression(wr_source.get(), "{wr_float} * 2"));

    Property_write_recording recording;
    EXPECT_EQ(Property_write_recording::get_active(), &recording);
    EXPECT_TRUE(recording.take_records().empty());
    EXPECT_EQ(recording.get_refusal_count(), std::size_t{0});
}

TEST(Property_write_recording, closes_with_its_scope)
{
    {
        const Property_write_recording recording;
        EXPECT_EQ(Property_write_recording::get_active(), &recording);
    }
    EXPECT_EQ(Property_write_recording::get_active(), nullptr);
}

TEST(Property_write_recording, first_before_and_last_after)
{
    Recording_object o;
    o.set_value(wr_float, 2.0f);

    Property_write_recording recording;
    o.set_value(wr_float, 3.0f);
    o.set_value(wr_source, 7.0f);
    o.set_value(wr_float, 4.0f);
    std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{2});
    EXPECT_EQ(records[0].object, &o);
    EXPECT_EQ(records[0].property, &wr_float.get());
    EXPECT_EQ(local_float(records[0].before), 2.0f);
    EXPECT_EQ(local_float(records[0].after), 4.0f);
    EXPECT_EQ(records[1].property, &wr_source.get());
    EXPECT_FALSE(records[1].before.has_value());
    EXPECT_EQ(local_float(records[1].after), 7.0f);

    // Taking empties the recording, which keeps recording.
    EXPECT_TRUE(recording.take_records().empty());
    o.set_value(wr_float, 5.0f);
    records = recording.take_records();
    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(local_float(records[0].before), 4.0f);
    EXPECT_EQ(local_float(records[0].after), 5.0f);
}

TEST(Property_write_recording, clear_to_unauthored)
{
    Recording_object o;
    o.set_value(wr_float, 2.0f);

    Property_write_recording recording;
    o.clear_value(wr_float);
    o.clear_value(wr_source); // no local value: nothing changes, nothing recorded
    const std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].property, &wr_float.get());
    EXPECT_EQ(local_float(records[0].before), 2.0f);
    EXPECT_FALSE(records[0].after.has_value());

    // Applying the before state restores the authored value.
    EXPECT_TRUE(o.apply_local_state(wr_float.get(), records[0].before));
    EXPECT_EQ(o.read_local_value(wr_float).value(), 2.0f);
}

TEST(Property_write_recording, expression_restore)
{
    Recording_object o;
    o.set_value(wr_source, 5.0f);
    EXPECT_TRUE(o.set_expression(wr_float.get(), "{wr_source} * 2"));
    EXPECT_EQ(o.get_value(wr_float), 10.0f);

    std::vector<Property_write_record> records;
    {
        Property_write_recording recording;
        // set_current_value keeps the expression: not a local-layer write.
        EXPECT_TRUE(o.set_current_value(wr_float.get(), Property_value{6.0f}));
        EXPECT_TRUE(recording.take_records().empty());

        o.set_value(wr_float, 7.0f);
        records = recording.take_records();
    }
    ASSERT_EQ(records.size(), std::size_t{1});
    ASSERT_TRUE(records[0].before.has_value());
    ASSERT_TRUE(std::holds_alternative<Expression_text>(records[0].before.value()));
    EXPECT_EQ(std::get<Expression_text>(records[0].before.value()).text, "{wr_source} * 2");
    EXPECT_EQ(local_float(records[0].after), 7.0f);

    EXPECT_TRUE(o.apply_local_state(wr_float.get(), records[0].before));
    EXPECT_EQ(o.get_expression(wr_float.get()).value(), "{wr_source} * 2");
    EXPECT_EQ(o.get_value(wr_float), 10.0f);

    // An expression installed inside a recording is recorded with the
    // value before it.
    {
        Property_write_recording recording;
        EXPECT_TRUE(o.set_expression(wr_source.get(), "4"));
        records = recording.take_records();
    }
    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].property, &wr_source.get());
    EXPECT_EQ(local_float(records[0].before), 5.0f);
    ASSERT_TRUE(records[0].after.has_value());
    EXPECT_EQ(std::get<Expression_text>(records[0].after.value()).text, "4");
}

TEST(Property_write_recording, bridged_property)
{
    Recording_object o;

    Property_write_recording recording;
    o.set_value(wr_name, std::string{"second"});
    o.set_value(wr_name, std::string{"third"});
    const std::vector<Property_write_record> records = recording.take_records();

    EXPECT_EQ(o.name, "third");
    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].property, &wr_name.get());
    ASSERT_TRUE(records[0].before.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<Property_value>(records[0].before.value())), "first");
    ASSERT_TRUE(records[0].after.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<Property_value>(records[0].after.value())), "third");
}

TEST(Property_write_recording, writable_computed_property_records_its_target_once)
{
    Recording_object o;

    Property_write_recording recording;
    EXPECT_TRUE(o.set_value(wr_double.get(), Property_value{8.0f}));
    EXPECT_TRUE(o.set_value(wr_double.get(), Property_value{10.0f}));
    const std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].property, &wr_target.get());
    EXPECT_FALSE(records[0].before.has_value());
    EXPECT_EQ(local_float(records[0].after), 5.0f);
}

TEST(Property_write_recording, refused_writes_are_counted_and_not_recorded)
{
    Recording_object o;
    Recording_object sealed;
    sealed.seal();

    Property_write_recording recording;
    o.set_value(wr_validated, 2.0f);                                   // validate
    o.set_value(wr_name, std::string{});                               // bridge validate
    EXPECT_FALSE(o.set_expression(wr_float.get(), "1 +"));             // expression syntax
    EXPECT_FALSE(o.set_value(wr_double.get(), Property_value{1}));     // type check (validate)
    sealed.set_value(wr_float, 2.0f);                                  // seal
    sealed.clear_value(wr_float);                                      // seal
    o.set_value(wr_float, 3.0f);                                       // accepted

    ASSERT_EQ(recording.take_records().size(), std::size_t{1});
    ASSERT_EQ(recording.get_refusal_count(), std::size_t{6});
    const std::span<const Property_write_refusal> refusals = recording.get_refusals();
    EXPECT_EQ(refusals[0].property, &wr_validated.get());
    EXPECT_EQ(refusals[0].object, &o);
    EXPECT_EQ(refusals[1].property, &wr_name.get());
    EXPECT_EQ(refusals[2].property, &wr_float.get());
    EXPECT_EQ(refusals[3].property, &wr_double.get());
    EXPECT_EQ(refusals[4].object, &sealed);
    EXPECT_EQ(refusals[5].object, &sealed);
    EXPECT_EQ(o.name, "first");
    EXPECT_FALSE(sealed.has_local_value(wr_float.get()));
}

TEST(Property_write_recording, propagation_to_descendants_is_not_recorded)
{
    Recording_object parent;
    Recording_object child;
    child.set_parent(&parent);

    Property_write_recording recording;
    parent.set_value(wr_inh, 4.0f);
    const std::vector<Property_write_record> records = recording.take_records();

    EXPECT_EQ(child.get_value(wr_inh), 4.0f);
    EXPECT_EQ(child.change_count("wr_inh"), std::size_t{1});
    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].object, &parent);
    child.set_parent(nullptr);
}

TEST(Property_write_recording, callback_writes_follow_the_write_that_caused_them)
{
    Recording_object o;
    o.set_value(wr_follower, 1.0f);

    Property_write_recording recording;
    o.set_value(wr_leader, 2.0f);
    const std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{2});
    EXPECT_EQ(records[0].property, &wr_leader.get());
    EXPECT_EQ(records[1].property, &wr_follower.get());
    EXPECT_EQ(local_float(records[1].before), 1.0f);
    EXPECT_EQ(local_float(records[1].after), 102.0f);
}

TEST(Property_write_recording, batch_closed_inside_the_recording_flushes_inside_it)
{
    Recording_object o;

    Property_write_recording recording;
    {
        const Dependency_object::Change_batch batch{o};
        o.set_value(wr_leader, 2.0f);
        o.set_value(wr_float, 3.0f);
        // The leader's changed callback is queued with the batch: the
        // follower is not written yet.
        EXPECT_FALSE(o.has_local_value(wr_follower.get()));
    }
    EXPECT_EQ(o.get_value(wr_follower), 102.0f);
    const std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{3});
    EXPECT_EQ(records[0].property, &wr_leader.get());
    EXPECT_EQ(records[1].property, &wr_float.get());
    EXPECT_EQ(records[2].property, &wr_follower.get());
    EXPECT_FALSE(records[2].before.has_value());
    EXPECT_EQ(local_float(records[2].after), 102.0f);
}

TEST(Property_write_recording, set_current_value_without_expression_is_recorded)
{
    Recording_object o;

    Property_write_recording recording;
    EXPECT_TRUE(o.set_current_value(wr_float.get(), Property_value{6.0f}));
    const std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].property, &wr_float.get());
    EXPECT_FALSE(records[0].before.has_value());
    EXPECT_EQ(local_float(records[0].after), 6.0f);
}

TEST(Property_write_recording, destroyed_object_leaves_no_record)
{
    Recording_object kept;

    Property_write_recording recording;
    {
        Recording_object destroyed;
        destroyed.set_value(wr_float, 2.0f);
        destroyed.set_value(wr_validated, 5.0f); // refused
        kept.set_value(wr_float, 3.0f);
        kept.set_value(wr_validated, 5.0f);      // refused
    }
    const std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].object, &kept);
    ASSERT_EQ(recording.get_refusal_count(), std::size_t{1});
    EXPECT_EQ(recording.get_refusals()[0].object, &kept);
}

TEST(Property_write_recording, key_write_of_read_only_property_is_not_recorded)
{
    Recording_object o;

    Property_write_recording recording;
    o.set_value(wr_read_only, 2.0f);
    EXPECT_EQ(o.get_value(wr_read_only.get_property()), 2.0f);
    o.clear_value(wr_read_only);
    EXPECT_FALSE(o.has_local_value(wr_read_only.get()));

    EXPECT_TRUE(recording.take_records().empty());
    EXPECT_EQ(recording.get_refusal_count(), std::size_t{0});
}

TEST(Property_write_recording, read_only_and_computed_refusals)
{
    Recording_object o;
    o.set_value(wr_source, 5.0f);
    EXPECT_TRUE(o.set_expression(wr_source.get(), "{wr_float}"));

    Property_write_recording recording;
    EXPECT_FALSE(o.set_value(wr_read_only.get(), Property_value{1.0f}));
    EXPECT_FALSE(o.clear_value(wr_read_only.get()));
    EXPECT_FALSE(o.set_expression(wr_read_only.get(), "1"));
    EXPECT_FALSE(o.set_value(wr_computed.get(), Property_value{1.0f}));
    EXPECT_FALSE(o.clear_value(wr_computed.get()));
    // wr_source reads wr_float: an expression on wr_float reading wr_source
    // would cycle. Its before state was read, then dropped with the refusal.
    EXPECT_FALSE(o.set_expression(wr_float.get(), "{wr_source}"));

    EXPECT_TRUE(recording.take_records().empty());
    ASSERT_EQ(recording.get_refusal_count(), std::size_t{6});
    const std::span<const Property_write_refusal> refusals = recording.get_refusals();
    EXPECT_EQ(refusals[0].property, &wr_read_only.get());
    EXPECT_EQ(refusals[1].property, &wr_read_only.get());
    EXPECT_EQ(refusals[2].property, &wr_read_only.get());
    EXPECT_EQ(refusals[3].property, &wr_computed.get());
    EXPECT_EQ(refusals[4].property, &wr_computed.get());
    EXPECT_EQ(refusals[5].property, &wr_float.get());
    EXPECT_FALSE(o.get_expression(wr_float.get()).has_value());

    // The refused cycle left the pair unrecorded: a later accepted write
    // records the state from before the refusal.
    o.set_value(wr_float, 4.0f);
    const std::vector<Property_write_record> records = recording.take_records();
    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_FALSE(records[0].before.has_value());
}

TEST(Property_write_recording, bridged_clear_is_recorded_once)
{
    Recording_object o;
    o.stored = 2.0f;

    Property_write_recording recording;
    EXPECT_TRUE(o.clear_value(wr_stored.get()));
    const std::vector<Property_write_record> records = recording.take_records();

    EXPECT_EQ(o.stored, 9.0f);
    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].property, &wr_stored.get());
    EXPECT_EQ(local_float(records[0].before), 2.0f);
    EXPECT_EQ(local_float(records[0].after), 9.0f);
}

TEST(Property_write_recording, writable_computed_through_apply_local_state)
{
    Recording_object o;

    Property_write_recording recording;
    EXPECT_TRUE(o.apply_local_state(wr_double.get(), Local_state{Property_value{6.0f}}));
    const std::vector<Property_write_record> records = recording.take_records();

    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].property, &wr_target.get());
    EXPECT_EQ(local_float(records[0].after), 3.0f);
}

TEST(Property_write_recording, clear_of_animated_only_entry_is_not_recorded)
{
    Recording_object o;
    EXPECT_TRUE(o.set_animated_value(wr_float, 5.0f));

    Property_write_recording recording;
    EXPECT_TRUE(o.clear_value(wr_float.get()));
    EXPECT_TRUE(recording.take_records().empty());
    EXPECT_EQ(o.get_value(wr_float), 5.0f);
}
