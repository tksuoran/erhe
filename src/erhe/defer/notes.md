# erhe_defer

## Purpose
Provides a scope-guard utility for deferred execution. When a `Defer` object goes out
of scope, its stored callback is invoked. This is the C++ equivalent of Go's `defer` statement.

## Key Types
- `Defer` -- RAII class that calls a `std::function<void()>` in its destructor.

## Public API
- `Defer(callback)` -- Construct with a lambda/function to call on destruction.
- `Defer::bind(callback)` -- Rebind the callback after default construction.
- `ERHE_DEFER(statements)` -- Macro that creates a uniquely-named `Defer` capturing `[&]`.
- `ERHE_DEFER_FUN(fun)` -- Macro variant taking a callable directly.

## Dependencies
- **erhe libraries:** None
- **External:** Standard library only (`<functional>`)

## Notes
- Move-only; copy is deleted.
- The moved-from `Defer` has its callback set to `nullptr`, so it will not invoke anything.
- No external dependencies at all; this is a leaf library.
