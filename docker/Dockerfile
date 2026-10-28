# Headless build environment for the F.A.C. V2 bootloader:
# Arm GNU Toolchain (same base as ST's "GNU Tools for STM32"), CMake, Ninja,
# and the host gcc for the unit tests.
FROM debian:bookworm-slim

ARG ARM_GNU_VERSION=13.3.rel1

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		ca-certificates curl xz-utils cmake ninja-build make gcc libc6-dev \
	&& rm -rf /var/lib/apt/lists/*

# Official Arm toolchain tarball, unpacked under /opt
RUN curl -fsSL -o /tmp/arm-gnu.tar.xz \
		"https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GNU_VERSION}/binrel/arm-gnu-toolchain-${ARM_GNU_VERSION}-x86_64-arm-none-eabi.tar.xz" \
	&& mkdir -p /opt/arm-gnu \
	&& tar -xJf /tmp/arm-gnu.tar.xz -C /opt/arm-gnu --strip-components=1 \
	&& rm /tmp/arm-gnu.tar.xz \
	&& rm -rf /opt/arm-gnu/share/doc /opt/arm-gnu/share/info /opt/arm-gnu/share/man

ENV PATH="/opt/arm-gnu/bin:${PATH}"

WORKDIR /work
