FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update
RUN apt install -y software-properties-common
RUN apt -y install \
    curl        \
    wget        \
    gcc         \
    g++         \
    gcc-10      \
    g++-10      \
    make        \
    automake    \
    autoconf    \
    zip         \
    libcurl4-openssl-dev \
    libssl-dev  \
    clang       \
    git         \
    ninja-build \
    python3     \
    xorg-dev    \
    libudev-dev

# build & install latest cmake
RUN git clone https://github.com/Kitware/CMake.git && \
    cd CMake && \
    git checkout ${CHECKOUT_COMMIT} && \
    ./bootstrap --prefix=/usr && \
    make && \
    make install

# Set locale
#RUN locale-gen en_US.UTF-8
#ENV LANG en_US.UTF-8
#ENV LC_ALL en_US.UTF-8

# Install Visual Studio Code requirements
RUN wget -O ~/vsls-reqs https://aka.ms/vsls-linux-prereq-script && chmod +x ~/vsls-reqs && ~/vsls-reqs
