FROM phusion/baseimage:0.9.19
MAINTAINER graphenelab

ENV LANG=en_US.UTF-8

ADD . /gravity-core
WORKDIR /gravity-core

RUN \
    apt-get update -y && \
    apt-get install -y \
      g++ \
      autoconf \
      cmake \
      git \
      libbz2-dev \
      libreadline-dev \
      libboost-all-dev \
      libcurl4-openssl-dev \
      libssl-dev \
      libncurses-dev \
      doxygen \
      libcurl4-openssl-dev \
      fish && \
    #
    # Obtain version
    echo && echo '------ Obtain version ------' && \
    mkdir -v  /etc/gravity /var/lib/gravity && \
    git submodule update --init --recursive && \
    git rev-parse --short HEAD > /etc/gravity/version && \

    #
    # Default exec/config files
    echo && echo '------ Default exec/config files ------' && \
    cp -fv docker/default_config.ini /etc/gravity/config.ini && \
    cp -fv docker/gravityentry.sh /usr/local/bin/gravityentry.sh && \
    chmod -v a+x /usr/local/bin/gravityentry.sh && \

    #
    # Compile
    # FIXME: doesn't build in release mode
    #  -DCMAKE_BUILD_TYPE=Release \
    echo && echo '------ Compile ------' && \
    cmake \
      . && \
    make witness_node cli_wallet && \
    make install && \
    cd / && \
    rm -rf /gravity-core && \
    apt-get autoremove -y --purge g++ gcc autoconf cmake libboost-all-dev doxygen && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Home directory $HOME
WORKDIR /
ENV HOME /var/lib/gravity

# Volume
VOLUME ["/var/lib/gravity"]

# rpc service:
EXPOSE 8090
# p2p service:
EXPOSE 9090

# default execute entry
CMD /usr/local/bin/gravityentry.sh
