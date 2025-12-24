FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    librabbitmq-dev \
    nlohmann-json3-dev \
    pkg-config \
    libboost-all-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*


WORKDIR /app
COPY common ./common
COPY websocket-service ./websocket-service

WORKDIR /app/websocket-service
RUN rm -rf build \
    && cmake -S include -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libssl3 \
    librabbitmq4 \
    libboost-system1.74.0 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/websocket-service/build/websocket-service /app/websocket-service

EXPOSE 8080
CMD ["/app/websocket-service"]
