# ---- Build stage ----
FROM gcc:13 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        make \
        git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt ./
COPY src/ src/
COPY tests/ tests/

RUN mkdir -p out && cd out \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" \
    && make -j"$(nproc)"

# ---- Runtime stage ----
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 \
        python3-pip \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy server binary
COPY --from=builder /build/out/wow-server-sim /app/wow-server-sim

# Install Python tooling
COPY tools/ /app/tools/
RUN python3 -m venv /app/.venv \
    && /app/.venv/bin/pip install --upgrade pip \
    && /app/.venv/bin/pip install -e /app/tools[dev]

ENV PATH="/app/.venv/bin:${PATH}"

# Copy demo and support scripts
COPY scripts/ /app/scripts/

# Game port, control channel, telemetry UDP
EXPOSE 8080 8081 9090/udp

ENTRYPOINT ["/app/wow-server-sim"]
