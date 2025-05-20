def current_folder():
    from pathlib import Path
    return Path(__file__).parent


def parent_folder():
    from pathlib import Path
    return Path(__file__).parent.parent


def copy_file_to_parent_directory(file_name):
    import shutil
    import os
    shutil.copyfile(os.path.join(current_folder(), file_name),
                    os.path.join(parent_folder(),  file_name))


def make_directory_if_necessary(path):
    import os
    if not os.path.exists(path):
        os.mkdir(path)


def remove_directory(path):
    import os
    if os.path.exists(path) and os.path.isdir(path):
        import shutil
        shutil.rmtree(path)


def make_clean_directory(path):
    import os
    remove_directory(path)
    os.mkdir(path)
