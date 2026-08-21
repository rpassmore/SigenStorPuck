FROM python:3.12-bookworm

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive
ENV PIP_DISABLE_PIP_VERSION_CHECK=1
ENV PLATFORMIO_CORE_DIR=/home/${USERNAME}/.platformio

# System dependencies:
# - build-essential: native compiler/toolchain
# - SDL2: desktop simulator
# - libusb/udev: USB/ESP32 access
# - git/curl/wget: PlatformIO and development tooling
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    curl \
    wget \
    pkg-config \
    libsdl2-dev \
    libusb-1.0-0-dev \
    usbutils \
    udev \
    ca-certificates \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root development user.
RUN groupadd --gid ${USER_GID} ${USERNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} \
       --create-home --shell /bin/bash ${USERNAME} \
    && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}

# Install PlatformIO and esptool globally in the Python environment.
RUN pip install --no-cache-dir \
    platformio \
    esptool

USER ${USERNAME}

WORKDIR /workspace

# Make PlatformIO available immediately.
ENV PATH="/usr/local/bin:${PATH}"

CMD ["bash"]