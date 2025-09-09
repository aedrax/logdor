FROM mcr.microsoft.com/devcontainers/universal:2-linux

# PIP requirement like "aqtinstall==2.0.6" or url with egg file
ARG AQT_VERSION="aqtinstall==3.3.0"

ARG QT_VERSION=6.8.3
# LINUXDEPLOY_COMMIT Sep 4, 2025
ARG LINUXDEPLOY_COMMIT=d2557d616749c325a61058049e41a2d35ffce924
# LINUXDEPLOY_QT_COMMIT Sep 4, 2025
ARG LINUXDEPLOY_QT_COMMIT=b0cb921a656c5630919d63f68953ee79fdeb0903
ARG QT_PATH=/opt/Qt

ARG ADDITIONAL_PACKAGES="sudo git openssh-client ca-certificates build-essential ninja-build curl python3 locales patchelf"

ENV DEBIAN_FRONTEND=noninteractive \
    DEBCONF_NONINTERACTIVE_SEEN=true \
    QT_VERSION=${QT_VERSION} \
    AQT_VERSION=${AQT_VERSION} \
    QT_PATH=${QT_PATH} \
    QT_GCC=${QT_PATH}/${QT_VERSION}/gcc_64 \
    PATH=${QT_PATH}/Tools/CMake/bin:${QT_PATH}/Tools/Ninja:${QT_PATH}/${QT_VERSION}/gcc_64/bin:$PATH

COPY get_qt.sh get_linuxdeploy.sh install_packages.sh /tmp/

# Get Qt binaries with aqt
RUN /tmp/get_qt.sh

# Get linuxdeploy and build it
RUN /tmp/get_linuxdeploy.sh

# Install the required packages
RUN /tmp/install_packages.sh

# Reconfigure locale
RUN locale-gen en_US.UTF-8 && dpkg-reconfigure locales