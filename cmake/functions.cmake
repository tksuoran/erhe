# ---- Add sources with source groups ----

#[==[
This function combines target_sources and source_group to conveniently add
sources to targets that are grouped.

The target argument is passed as is to target_sources.

The following arguments until FILES will be grouped into ${group_args}.
Please note that empty arguments that would go into ${group_args} cannot be
handled. You may use a slash to put things at root level instead.

Everything after the FILES argument will be processed one by one as follows:

source_group(${group_args} FILES "${arg}")
target_sources("${target}" PRIVATE "${arg}")

As you can see, only the FILES form of source_group is allowed, but this also
means that you cannot group sources with generator expressions in them.
]==]
function(erhe_target_sources_grouped target)
  if(ARGC LESS "4")
    message(FATAL_ERROR "Too few arguments (given: ${ARGC})")
  endif()

  math(EXPR limit "${ARGC} - 1")
  set(idx "${ARGC}")
  set(group_args "")
  set(seen_empty NO)

  foreach(i RANGE 1 "${limit}")
    set(arg "${ARGV${i}}")
    if(arg STREQUAL "FILES")
      math(EXPR idx "${i} + 1")
      break()
    elseif(arg STREQUAL "")
      if(NOT seen_empty)
        message(AUTHOR_WARNING "Empty argument found. This can't be handled.")
        set(seen_empty YES)
      endif()
      continue()
    endif()
    string(REPLACE ";" "\\;" arg "${arg}")
    list(APPEND group_args "${arg}")
  endforeach()

  if(idx EQUAL ARGC)
    message(FATAL_ERROR "Incorrect arguments")
  endif()

  foreach(i RANGE "${idx}" "${limit}")
    #message("source_group(${group_args} FILES \"${ARGV${i}}\")")
    source_group(${group_args} FILES "${ARGV${i}}")
    target_sources("${target}" PRIVATE "${ARGV${i}}")
  endforeach()
endfunction()
