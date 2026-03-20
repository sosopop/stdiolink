FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG STDIOLINK_RELEASE_DIR=/srv/stdiolink-release

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        ca-certificates \
        libasound2t64 \
        libdbus-1-3 \
        libegl1 \
        libfontconfig1 \
        libfreetype6 \
        libgl1 \
        libglib2.0-0t64 \
        libglu1-mesa \
        libice6 \
        libicu74 \
        libjpeg-turbo8 \
        libnss3 \
        libopengl0 \
        libpng16-16t64 \
        libpulse0 \
        libsm6 \
        libssl3t64 \
        libudev1 \
        libx11-6 \
        libx11-xcb1 \
        libxcb-glx0 \
        libxcb-icccm4 \
        libxcb-image0 \
        libxcb-keysyms1 \
        libxcb-randr0 \
        libxcb-render-util0 \
        libxcb-shape0 \
        libxcb-shm0 \
        libxcb-sync1 \
        libxcb-util1 \
        libxcb-xfixes0 \
        libxcb-xinerama0 \
        libxcb-xkb1 \
        libxcb1 \
        libxext6 \
        libxfixes3 \
        libxi6 \
        libxkbcommon0 \
        libxkbcommon-x11-0 \
        libxrender1 \
        procps \
        zlib1g \
    && rm -rf /var/lib/apt/lists/*

ENV STDIOLINK_RELEASE_DIR="${STDIOLINK_RELEASE_DIR}"

WORKDIR /work

COPY docker_entrypoint.sh /usr/local/bin/stdiolink-docker-entrypoint

RUN chmod +x /usr/local/bin/stdiolink-docker-entrypoint \
    && mkdir -p "${STDIOLINK_RELEASE_DIR}"

EXPOSE 6200

ENTRYPOINT ["/usr/local/bin/stdiolink-docker-entrypoint"]
