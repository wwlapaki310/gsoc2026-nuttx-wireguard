# WireGuard peer for the hardware demos, run under Docker Desktop on a PC
# without admin rights (the Linux VM's kernel has WireGuard; the container
# only needs NET_ADMIN). See scripts/stackchan/wg_setup.py.
FROM ubuntu:24.04
RUN apt-get update -q && apt-get install -y --no-install-recommends \
    wireguard-tools iproute2 iputils-ping netcat-openbsd curl socat \
  && rm -rf /var/lib/apt/lists/*
CMD ["sleep", "infinity"]
