FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="3.22.2"

# Optionally install the cmake for vcpkg
COPY ./reinstall-cmake.sh /tmp/

RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
  chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
  fi \
  && rm -f /tmp/reinstall-cmake.sh

# [Optional] Uncomment this section to install additional vcpkg ports.
# RUN su vscode -c "${VCPKG_ROOT}/vcpkg install <your-port-name-here>"

RUN apt-get update
RUN export DEBIAN_FRONTEND=noninteractive \
  && apt-get -y install --no-install-recommends libnuma-dev libyaml-dev libssl-dev ninja-build clang-15 linux-perf clang-16 libclang-rt-16-dev


WORKDIR /tmp
RUN git clone https://github.com/duckdb/duckdb
WORKDIR /tmp/duckdb
RUN GEN=ninja make
RUN cp /tmp/duckdb/src/include/duckdb_extension.h /tmp/duckdb/src/include/duckdb.h /tmp/duckdb/src/include/duckdb.hpp /usr/local/include/
RUN cp /tmp/duckdb/build/release/src/libduckdb.so /tmp/duckdb/build/release/src/libduckdb_static.a  /usr/local/lib/

EXPOSE 8080
