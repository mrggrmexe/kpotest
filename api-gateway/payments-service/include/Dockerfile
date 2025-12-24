FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpqxx-dev \
    libssl-dev \
    librabbitmq-dev \
    nlohmann-json3-dev \
    pkg-config \
    wget \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp
RUN wget https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.12.4.tar.gz \
    && tar -xzf v0.12.4.tar.gz \
    && cp cpp-httplib-0.12.4/httplib.h /usr/local/include/

WORKDIR /app
COPY . .

RUN cmake -S include -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libpqxx-6.4 \
    libssl3 \
    librabbitmq4 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/payments-service /app/payments-service

EXPOSE 8080

CMD ["/app/payments-service"]
