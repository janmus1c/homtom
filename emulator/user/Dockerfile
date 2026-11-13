FROM debian:trixie-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends python3 python3-pil python3-numpy iproute2 \
  && rm -rf /var/lib/apt/lists/*
COPY supervisor.py index.html barcelona.txt shim.so fbdemo /emu/
COPY guest /emu/guest
EXPOSE 8080 23
ENTRYPOINT ["python3", "/emu/supervisor.py"]
