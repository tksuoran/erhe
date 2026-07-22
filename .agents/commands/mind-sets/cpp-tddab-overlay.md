# C++ TDDAB Overlay

**OVERLAY - extends tddab-planner.md with C++-specific patterns. Do NOT repeat base TDDAB principles.**

## Test Framework

- **Unit tests:** Catch2 v3 (or GoogleTest / doctest - match the project)
- **Assertions:** `REQUIRE`, `CHECK`, `REQUIRE_THROWS_AS` (Catch2) / `EXPECT_*`, `ASSERT_*` (GoogleTest)
- **Mocking:** GoogleMock, or hand-written fakes implementing the interface
- **Runner:** `ctest` (CTest drives the test executables)

## Build & Test Commands

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -L fast            # unit tests only
ctest --test-dir build -R order           # filter by name
# coverage: configure with --coverage (gcc/clang) then run gcovr -r . build
```

## RED Phase - C++ Pattern

```cpp
#include <catch2/catch_test_macros.hpp>
#include "order_service.hpp"

TEST_CASE("create_order with valid items succeeds", "[order][fast]")
{
    Order_service sut;
    const std::vector<Order_item> items{ Order_item{"SKU-1", 2} };

    const std::expected<Order, Error> result = sut.create_order(items);

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->id().empty());
    REQUIRE(result->items().size() == 1);
    REQUIRE(result->status() == Order_status::created);
}

TEST_CASE("create_order with empty items returns error", "[order][fast]")
{
    Order_service sut;

    const std::expected<Order, Error> result = sut.create_order({});

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code() == Error_code::validation);
}
```

## GREEN Phase - C++ Pattern

```cpp
// order_service.hpp
#include <expected>
#include <string>
#include <vector>

enum class Order_status { created };

class Order_item {
public:
    Order_item(std::string sku, std::uint32_t quantity)
        : m_sku{std::move(sku)}, m_quantity{quantity} {}
private:
    std::string   m_sku;
    std::uint32_t m_quantity;
};

class Order {
public:
    Order(std::string id, std::vector<Order_item> items, Order_status status)
        : m_id{std::move(id)}, m_items{std::move(items)}, m_status{status} {}
    [[nodiscard]] auto id    () const -> const std::string&            { return m_id; }
    [[nodiscard]] auto items () const -> const std::vector<Order_item>& { return m_items; }
    [[nodiscard]] auto status() const -> Order_status                  { return m_status; }
private:
    std::string             m_id;
    std::vector<Order_item> m_items;
    Order_status            m_status;
};

class Order_service {
public:
    [[nodiscard]] auto create_order(std::vector<Order_item> items) -> std::expected<Order, Error>
    {
        if (items.empty()) {
            return std::unexpected{Error{Error_code::validation, "order must have at least one item"}};
        }
        return Order{generate_id(), std::move(items), Order_status::created};
    }
};
```

## VERIFY Phase

```bash
cmake --build build                                  # 0 warnings / 0 errors
ctest --test-dir build --output-on-failure           # all green
clang-tidy -p build $(git ls-files '*.cpp')          # static analysis clean
ctest --test-dir build-asan                           # ASan/UBSan build - no leaks / UB
```

## C++-Specific TDDAB Rules

1. **expected/optional:** always test both the value and the error/empty path.
2. **Throwing paths:** assert with `REQUIRE_THROWS_AS(expr, ExceptionType)` and check the message/code.
3. **Test target per module:** each `add_library` has one `add_executable(<lib>_tests)` + `add_test`.
4. **clang-tidy + sanitizers:** run in the VERIFY phase - zero warnings, no ASan/UBSan reports.
5. **RAII in tests:** construct the SUT on the stack; let destructors clean up - no manual `delete`.
6. **Value semantics:** test that copies/moves behave (especially with owning members and `operator==`).
7. **No allocation assertions:** for hot-path code, cover the cleared-scratch reuse path, not just correctness.
