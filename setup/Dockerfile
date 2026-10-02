# The environment this repository builds and runs in: the RISC-V toolchain, the
# proxy kernel, and the pinned Spike and gem5, already built. Built by setup/image.sh.
ARG BASE_IMAGE

FROM ${BASE_IMAGE} AS system
ARG VCIX_ENV_ROOT
ENV VCIX_ENV_ROOT=${VCIX_ENV_ROOT}
COPY versions.env system.sh /opt/vcix-setup/
RUN /opt/vcix-setup/system.sh

FROM system AS build
ARG JOBS=4
COPY setup.sh /opt/vcix-setup/
RUN /opt/vcix-setup/setup.sh -j "$JOBS" toolchain pk spike gem5

FROM build AS slim
COPY slim.sh /opt/vcix-setup/
RUN /opt/vcix-setup/slim.sh

FROM system
COPY --from=slim ${VCIX_ENV_ROOT} ${VCIX_ENV_ROOT}
LABEL org.opencontainers.image.source=https://github.com/PSAL-POSTECH/vcix-accelerator
LABEL org.opencontainers.image.description="Build and run environment for vcix-accelerator: RISC-V toolchain, pk, Spike and gem5"
