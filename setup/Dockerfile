# The environment this repository builds and runs in: the RISC-V toolchain, the
# proxy kernel, and the pinned Spike and gem5, already built. The repository
# itself is not in it -- a checkout is built against it (scripts/build.sh).
#
# Built by setup/image.sh, which passes the arguments below from versions.env;
# the build context is setup/, so nothing outside it can change the image.
ARG BASE_IMAGE

# The system packages. Also the final image's base: building this repository
# needs the same compilers that building the simulators does.
FROM ${BASE_IMAGE} AS system
ARG VCIX_ENV_ROOT
ENV VCIX_ENV_ROOT=${VCIX_ENV_ROOT}
COPY versions.env system.sh /opt/vcix-setup/
RUN /opt/vcix-setup/system.sh

# Everything is built here, as setup.sh leaves it on any machine.
FROM system AS build
ARG JOBS=4
COPY setup.sh /opt/vcix-setup/
RUN /opt/vcix-setup/setup.sh -j "$JOBS" toolchain pk spike gem5

# And cut down here, so the 6 GB gem5 build tree is never in a layer that ships.
FROM build AS slim
COPY slim.sh /opt/vcix-setup/
RUN /opt/vcix-setup/slim.sh

FROM system
COPY --from=slim ${VCIX_ENV_ROOT} ${VCIX_ENV_ROOT}
LABEL org.opencontainers.image.source=https://github.com/PSAL-POSTECH/vcix-accelerator
LABEL org.opencontainers.image.description="Build and run environment for vcix-accelerator: RISC-V toolchain, pk, Spike and gem5"
