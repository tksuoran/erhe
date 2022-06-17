FROM ubuntu:20.04

RUN DEBIAN_FRONTEND=noninteractive apt update

RUN DEBIAN_FRONTEND=noninteractive apt -y install \
    curl        \
    wget        \
    gcc-10      \
    g++-10      \
    clang-12    \
    git         \
    ninja-build \
    python3     \
    xorg-dev

# Install Visual Studio Code requirements
RUN wget -O ~/vsls-reqs https://aka.ms/vsls-linux-prereq-script && chmod +x ~/vsls-reqs && ~/vsls-reqs

# VisualStudio Code requires newer CMake.
# This is from https://apt.kitware.com/
#
# Please follow instructions on that page instead of executing steps below.
# Steps here have been modified for docker

# Step 1.
RUN DEBIAN_FRONTEND=noninteractive apt -y install gpg

# Step 2.
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null

# Step 3.
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null

RUN DEBIAN_FRONTEND=noninteractive apt update

# Step 4.
RUN rm /usr/share/keyrings/kitware-archive-keyring.gpg
RUN DEBIAN_FRONTEND=noninteractive apt -y install kitware-archive-keyring

# Final step
RUN DEBIAN_FRONTEND=noninteractive apt -y install cmake
