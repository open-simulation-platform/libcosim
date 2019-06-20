FROM ubuntu:18.04

COPY sources-18.04-azure.list /etc/apt/sources.list

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    doxygen \
    git \
    flex \
    python3-setuptools \
    python3-wheel \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Install  Conan package manager and CMake
RUN pip3 install conan cmake
