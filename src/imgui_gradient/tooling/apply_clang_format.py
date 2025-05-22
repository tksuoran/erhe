

def allowed_extensions():
    return [
        "c",
        "cpp",
        "h",
        "hpp",
        "inl",
        "tpp",
        "glsl",
        "frag",
        "vert",
        "comp",
        "geom",
        "tese",
        "tesc",
    ]


def extension(file):
    from pathlib import Path
    return Path(file).suffix.strip('.')


def run_clang_format_on_one_folder(folder, ignored=[], print_result=True):
    import os
    from os import listdir
    from os.path import join, isfile
    for file in listdir(folder):
        path = join(folder, file)
        if isfile(path) and extension(path) in allowed_extensions() and not file in ignored:
            if (print_result):
                print(path)
            os.chdir(folder)
            os.system(f"clang-format -style=file -i {file}")


def run_clang_format_on_folder(folder, ignored=[], print_result=True):
    import os
    for subfolder in [x[0] for x in os.walk(folder)]:
        run_clang_format_on_one_folder(subfolder, ignored, print_result)


def parent_folder():
    from pathlib import Path
    return Path(__file__).parent.parent


def apply_clang_format(folder, ignored=[], print_result=True):
    import os
    if print_result:
        from termcolor import colored

    path = os.path.join(parent_folder(), folder)
    if (os.path.isdir(path)):
        if print_result:
            print(colored(
                f"Applying clang-format on '{folder}' (Full path: '{path}')",
                'green'))
        run_clang_format_on_folder(path, ignored, print_result)
    else:
        if print_result:
            print(colored(
                f"Applying clang-format on '{folder}' FAILED. We did not find '{path}'",
                'red'))


if __name__ == '__main__':
    apply_clang_format('src')
    apply_clang_format('include')
    apply_clang_format('res')
    apply_clang_format('test')
    apply_clang_format('tests')
    apply_clang_format('example')
    apply_clang_format('examples')
