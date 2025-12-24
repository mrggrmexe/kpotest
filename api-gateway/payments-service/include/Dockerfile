FROM ubuntu:22.04 AS builder


RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libpqxx-dev \
    libssl-dev \
    librabbitmq-dev \
    nlohmann-json3-dev \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY common ./common
COPY payments-service ./payments-service

WORKDIR /app/payments-service
RUN rm -rf build \
    && cmake -S include -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libpqxx-6.4 \
    libssl3 \
    librabbitmq4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/payments-service/build/payments-service /app/payments-service

EXPOSE 8080
CMD ["/app/payments-service"]
