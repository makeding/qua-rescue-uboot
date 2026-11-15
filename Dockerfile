FROM debian:trixie

RUN apt-get update && apt-get install -y --no-install-recommends \
    bc bison build-essential flex gcc-arm-linux-gnueabi libssl-dev make \
    python3 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
