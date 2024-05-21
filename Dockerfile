FROM ubuntu:20.04
LABEL maintainer "pystyle"

ENV LC_ALL C.UTF-8
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libboost-all-dev

RUN rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/nekobean/mahjong-cpp.git
RUN chmod +x /mahjong-cpp/bin/server

EXPOSE 8888
CMD /mahjong-cpp/bin/server
