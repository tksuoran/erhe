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

  # If TREE is specified, resolve relative file paths against the tree root
  # so that both source_group and target_sources use the same base directory.
  set(_tree_path "")
  list(FIND group_args "TREE" _tree_idx)
  if(NOT _tree_idx EQUAL -1)
    math(EXPR _tree_val_idx "${_tree_idx} + 1")
    list(GET group_args ${_tree_val_idx} _tree_path)
  endif()

  foreach(i RANGE "${idx}" "${limit}")
    set(_file "${ARGV${i}}")
    if(_tree_path AND NOT IS_ABSOLUTE "${_file}")
      set(_file "${_tree_path}/${_file}")
    endif()
    #message("source_group(${group_args} FILES \"${_file}\")")
    source_group(${group_args} FILES "${_file}")
    target_sources("${target}" PRIVATE "${_file}")
  endforeach()
endfunction()

# ---- Download a file with a pinned content hash ----

#[==[
Downloads URI to FILE, unless FILE already exists with the expected SHA256.

The download goes to a temporary file that is only renamed into place once the
content hash matches, so a failed or truncated transfer never leaves a file
behind that would make a later configure skip the download.

Note that file(DOWNLOAD) reports success for HTTP error responses - the body of
an error page is transferred just fine - so the hash is the actual guard here.

DESCRIPTION is used in the progress and error messages.
]==]
function(erhe_download_pinned_file)
  cmake_parse_arguments(PARSE_ARGV 0 _arg "" "URI;FILE;SHA256;DESCRIPTION" "")
  foreach(_required URI FILE SHA256 DESCRIPTION)
    if(NOT _arg_${_required})
      message(FATAL_ERROR "erhe_download_pinned_file: missing ${_required} argument")
    endif()
  endforeach()

  if(EXISTS "${_arg_FILE}")
    file(SHA256 "${_arg_FILE}" _existing_hash)
    if(_existing_hash STREQUAL _arg_SHA256)
      return()
    endif()
    message(STATUS "${_arg_DESCRIPTION} does not match the pinned hash, downloading it again")
  endif()

  message(STATUS "Downloading ${_arg_DESCRIPTION}...")
  set(_temporary_file "${_arg_FILE}.download")
  file(REMOVE "${_temporary_file}")
  file(DOWNLOAD "${_arg_URI}" "${_temporary_file}" STATUS _status SHOW_PROGRESS)
  list(GET _status 0 _status_code)
  list(GET _status 1 _status_string)
  if(NOT _status_code EQUAL 0)
    file(REMOVE "${_temporary_file}")
    message(FATAL_ERROR "Failed to download ${_arg_DESCRIPTION} from ${_arg_URI} : ${_status_string}")
  endif()

  file(SHA256 "${_temporary_file}" _downloaded_hash)
  if(NOT _downloaded_hash STREQUAL _arg_SHA256)
    file(REMOVE "${_temporary_file}")
    message(
      FATAL_ERROR
      "Downloaded ${_arg_DESCRIPTION} from ${_arg_URI} does not match the pinned hash.\n"
      "  Expected SHA256 : ${_arg_SHA256}\n"
      "  Actual SHA256   : ${_downloaded_hash}\n"
      "The server most likely answered with an error page instead of the file."
    )
  endif()

  file(RENAME "${_temporary_file}" "${_arg_FILE}")
endfunction()
