#pragma once

// The profiler-aware mutex macros alone, for a header that declares a
// mutex and nothing else of the profiler (erhe_item/item_host.hpp):
// profile.hpp carries the zone / GPU / allocation macros and, on OpenGL,
// the gl loader include, which such a header has no use for.
// profile.hpp includes this file, so the two never disagree.

#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE)
#   include <tracy/Tracy.hpp>
#   define ERHE_PROFILE_MUTEX_DECLARATION(Type, mutex_variable) tracy::Lockable<Type> mutex_variable
#   define ERHE_PROFILE_MUTEX(Type, mutex_variable) TracyLockable(Type, mutex_variable)
#   define ERHE_PROFILE_LOCKABLE_BASE(Type) LockableBase(Type)
#else
#   define ERHE_PROFILE_MUTEX_DECLARATION(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_MUTEX(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_LOCKABLE_BASE(Type) Type
#endif
