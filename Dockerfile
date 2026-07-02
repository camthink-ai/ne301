FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC \
    CUBE_PROG_ROOT=/usr/local/STMicroelectronics/STM32CubeProgrammer \
    STEDGEAI_ROOT=/opt/stedgeai \
    ARM_GCC_DIR=/opt/arm-gnu-toolchain \
    GCC_PATH=/opt/arm-gnu-toolchain/bin

# Install base dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    apt-utils ca-certificates curl unzip xz-utils tar git make cmake build-essential \
    libusb-1.0-0 udev python3 python3-pip python3-dev \
    default-jre-headless pv \
    libx11-6 libx11-xcb1 libxext6 libxrender1 libxi6 libdbus-1-3 \
    libfontconfig1 libfreetype6 libglib2.0-0 libxkbcommon0 libxkbcommon-x11-0 \
    libxcb1 libxcb-util1 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render0 libxcb-render-util0 libxcb-shape0 \
    libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 \
    && rm -rf /var/lib/apt/lists/*

FROM builder AS install-gcc

ARG ARM_GCC_VERSION=13.3.rel1
ARG ARM_GCC_DIR=/opt/arm-gnu-toolchain
# Install ARM GCC toolchain (curl only, with progress)
RUN set -e && \
    URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GCC_VERSION}/binrel/arm-gnu-toolchain-${ARM_GCC_VERSION}-x86_64-arm-none-eabi.tar.xz" && \
    echo "Downloading ARM GCC toolchain from: $URL" && \
    curl -fSL --retry 3 --connect-timeout 60 --progress-bar "$URL" -o /tmp/gcc.tar.xz && \
    echo "Extracting ARM GCC toolchain..." && \
    pv /tmp/gcc.tar.xz | tar -xJf - -C /opt && \
    mv /opt/arm-gnu-toolchain-*-arm-none-eabi ${ARM_GCC_DIR} && \
    ln -s ${ARM_GCC_DIR}/bin/* /usr/local/bin/ && \
    rm /tmp/gcc.tar.xz && \
    echo "ARM GCC toolchain installed successfully"

FROM install-gcc AS install-node
# Install Node.js and pnpm (with progress)
RUN curl -fSL --progress-bar https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && npm install -g pnpm@latest \
    && rm -rf /var/lib/apt/lists/*

FROM install-node AS install-cubeprog
# Download and extract STM32CubeProgrammer from zip
ARG CUBEPROG_URL=https://resources.camthink.ai/tools/stm32cubeprg-lin-v2-19-0.zip
RUN set -e && \
    URL="${CUBEPROG_URL}" && \
    echo "Downloading STM32CubeProgrammer from: $URL" && \
    curl -fSL --retry 3 --connect-timeout 60 --progress-bar "$URL" -o /tmp/stm32cubeprg.zip && \
    unzip -q /tmp/stm32cubeprg.zip -d /tmp/cubeprog-extract && \
    INSTALLER=$(find /tmp/cubeprog-extract -name "SetupSTM32CubeProgrammer-*.linux" -type f | head -1) && \
    [ -n "$INSTALLER" ] && [ -f "$INSTALLER" ] || (echo "Error: Installer not found!" && exit 1) && \
    echo "Found installer: $INSTALLER" && \
    chmod +x "$INSTALLER" && \
    "$INSTALLER" -options-template /tmp/options && \
    sed -i "s|^INSTALL_PATH=.*|INSTALL_PATH=${CUBE_PROG_ROOT}|" /tmp/options && \
    echo -e "createDesktopLink=0\nlaunchprogram=0\nkeepLaunchingScript=0" >> /tmp/options && \
    "$INSTALLER" -options /tmp/options -console && \
    cp ${CUBE_PROG_ROOT}/Drivers/rules/*.rules /etc/udev/rules.d/ 2>/dev/null || true && \
    rm -rf /tmp/cubeprog-extract && \
    rm -rf /tmp/stm32cubeprg.zip && \
    echo "STM32CubeProgrammer installed successfully"

FROM install-cubeprog AS install-stedgeai
# Install ST Edge AI using online installer
# Build with: docker build --build-arg STEDGEAI_VARIANT=4.0 -t camthink/ne301-dev:v4.0 .
ENV QT_QPA_PLATFORM=minimal
ARG STEDGEAI_VARIANT=4.0
ARG STEDGEAI_PACKAGE=
ARG STEDGEAI_URL=
RUN set -e && \
    case "${STEDGEAI_VARIANT}" in \
      2.2) \
        PKG="${STEDGEAI_PACKAGE:-stedgeai0202.stneuralart}"; \
        URL="${STEDGEAI_URL:-https://resources.camthink.ai/tools/stedgeai-lin-v2-2-0.zip}"; \
        ;; \
      4.0) \
        PKG="${STEDGEAI_PACKAGE:-stedgeai0400.stm32mcu}"; \
        URL="${STEDGEAI_URL:-https://resources.camthink.ai/tools/stedgeai-lin-v4.0.1.zip}"; \
        ;; \
      *) \
        PKG="${STEDGEAI_PACKAGE:-stedgeai0300.stneuralart}"; \
        URL="${STEDGEAI_URL:-https://resources.camthink.ai/tools/stedgeai-lin-v3.0.0.zip}"; \
        ;; \
    esac && \
    echo "ST Edge AI variant: ${STEDGEAI_VARIANT} (${PKG})" && \
    echo "Downloading ST Edge AI from: $URL" && \
    curl -fSL --retry 3 --connect-timeout 60 --progress-bar "$URL" -o /tmp/stedgeai.zip && \
    unzip -q /tmp/stedgeai.zip -d /tmp/stedgeai-extract && \
    INSTALLER=$(find /tmp/stedgeai-extract -name "stedgeai-linux-onlineinstaller*" -type f | head -1) && \
    [ -n "$INSTALLER" ] && [ -f "$INSTALLER" ] || (echo "Error: stedgeai installer not found!" && exit 1) && \
    echo "Found installer: $INSTALLER" && \
    chmod +x "$INSTALLER" && \
    "$INSTALLER" --root ${STEDGEAI_ROOT} --accept-licenses --confirm-command --default-answer install ${PKG} && \
    VERSION_DIR=$(find ${STEDGEAI_ROOT} -maxdepth 1 -type d -name "[0-9]*" | head -1) && \
    [ -n "$VERSION_DIR" ] && [ -d "$VERSION_DIR" ] || (echo "Error: Version directory not found in ${STEDGEAI_ROOT}" && ls -la ${STEDGEAI_ROOT} && exit 1) && \
    echo "Found version directory: $VERSION_DIR" && \
    ([ -d "${VERSION_DIR}/Utilities/linux" ] && echo "export PATH=\$PATH:${VERSION_DIR}/Utilities/linux" >> /etc/profile.d/stedgeai.sh || true) && \
    echo "export STEDGEAI_CORE_DIR=${VERSION_DIR}" >> /etc/profile.d/stedgeai.sh && \
    rm -rf /tmp/stedgeai-extract && \
    rm -rf /tmp/stedgeai.zip && \
    echo "ST Edge AI installed successfully"

FROM install-stedgeai AS final
ARG STEDGEAI_VARIANT=4.0
# Set PATH
ENV PATH="${PATH}:${GCC_PATH}:${CUBE_PROG_ROOT}/bin" \
    STEDGEAI_VARIANT=${STEDGEAI_VARIANT}

# Cleanup and set working directory
RUN rm -rf /tmp/*
WORKDIR /workspace

# Copy entrypoint script
COPY docker-entrypoint.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["/bin/bash"]

LABEL maintainer="CamThink Development Team" \
      description="NE301 STM32N6570 AI Vision Camera Build Environment" \
      version="1.0" \
      stedgeai.variant="${STEDGEAI_VARIANT}"

