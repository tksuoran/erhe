from internal_utils import *

# .github/FUNDING.yml is a file that is used by GitHub to show on your repository all the links where people can sponsor you.
# https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/displaying-a-sponsor-button-in-your-repository

def copy_funding_file():
    import os
    make_directory_if_necessary(os.path.join(parent_folder(), ".github"))
    copy_file_to_parent_directory(os.path.join(".github", "FUNDING.yml"))

copy_funding_file()