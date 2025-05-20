from internal_utils import *

def copy_clang_format():
    copy_file_to_parent_directory(".clang-format")

def copy_clang_tidy():
    copy_file_to_parent_directory(".clang-tidy")

copy_clang_format()
copy_clang_tidy()