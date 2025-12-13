FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|archive.ubuntu.com|ports.ubuntu.com|g; s|security.ubuntu.com|ports.ubuntu.com|g' /etc/apt/sources.list && \
    apt-get update -o Acquire::Retries=5 && \
    apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      pkg-config \
      git \
      ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . /app

# Build with tests enabled and Ninja generator
RUN cmake -S . -B build -G Ninja -DTINYBUF_BUILD_TESTS=ON
RUN cmake --build build -j$(nproc)

# Run ctest; show failure output
RUN ctest --test-dir build --output-on-failure -j$(nproc)

CMD ["/bin/bash", "-lc", "ctest --test-dir build --output-on-failure -j$(nproc)"]
