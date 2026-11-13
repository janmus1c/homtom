FROM --platform=linux/386 debian:bookworm-slim@sha256:99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends make gcc libc6-dev perl file binutils && dpkg-query -W > /build-package-versions.txt && rm -rf /var/lib/apt/lists/*
WORKDIR /work
