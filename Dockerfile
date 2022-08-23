FROM ubuntu:22.04

RUN DEBIAN_FRONTEND=noninteractive apt update

RUN DEBIAN_FRONTEND=noninteractive apt -y install \
    curl        \
    cmake       \
    wget        \
    gcc         \
    g++         \
    clang       \
    git         \
    ninja-build \
    python3     \
    xorg-dev

# Install Visual Studio Code requirements
RUN wget -O ~/vsls-reqs https://aka.ms/vsls-linux-prereq-script && chmod +x ~/vsls-reqs && ~/vsls-reqs
