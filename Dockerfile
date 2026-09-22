FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# Configure multiarch for arm64 cross-compilation
RUN dpkg --add-architecture arm64 && \
    sed -i 's/^deb http/deb [arch=amd64] http/g' /etc/apt/sources.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal-updates main restricted universe multiverse" >> /etc/apt/sources.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    pkg-config \
    git \
    ca-certificates \
    python3 \
    libsdl2-dev:arm64 \
    libasound2-dev:arm64 \
    libfreetype6-dev:arm64 \
    libgles2-mesa-dev:arm64 \
    libegl1-mesa-dev:arm64 \
    libudev-dev:arm64 \
    libvulkan-dev:arm64 \
    kmod \
    && rm -rf /var/lib/apt/lists/*

COPY build.sh /build.sh
RUN chmod +x /build.sh
COPY patches/ /patches/
COPY common_src/ /common_src/

WORKDIR /build
ENTRYPOINT ["/build.sh"]
