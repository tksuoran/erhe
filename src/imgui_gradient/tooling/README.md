# tooling

Here are all the common things that we use when writing code.

You can simply run *setup_all.py* to setup everything automatically.<br/>
Currently this script does:
- copy *.clang-format* to the parent directory[^1]
- copy *.clang-tidy* to the parent directory[^1]
- copy *.github/FUNDING.yml* to the parent directory

If you need only some of the setup you can run *setup_dev_tools.py* and/or *setup_funding_file.py* individually.

[^1]: Because it won't get detected unless it is in a parent directory of your C++ source files. This is also cool because it allows you to tweak the copied version and commit it. Basically it allows you to modify our version without having to do a fork.

## Formatter

We use *ClangFormat* as our [formatting tool](https://julesfouchy.github.io/Learn--Clean-Code-With-Cpp/lessons/formatting-tool/) and we provide a *.clang-format* file to configure it.

## Static analyser

We use *ClangTidy* as our [static analysis tool](https://julesfouchy.github.io/Learn--Clean-Code-With-Cpp/lessons/static-analysers/) and we provide a *.clang-tidy* file to configure it.