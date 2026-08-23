# Reproducible toolchain for WarpSim. Usage:
#   docker build -t warpsim .
#   docker run --rm warpsim make quickstart
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++-13 clang-18 cmake git ca-certificates \
        python3 python3-dev python3-pip python3-numpy python3-pytest \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --break-system-packages clang-format==22.1.8 clang-tidy==22.1.8

ENV CC=gcc-13 CXX=g++-13
WORKDIR /workspace
COPY . .
CMD ["make", "quickstart"]
