FROM ubuntu:14.04

# original script: https://github.com/maddouri/cling-ubuntu-docker

ADD ./build-cling.sh /root/build-cling.sh

RUN /usr/bin/env bash /root/build-cling.sh

ENV PATH              "$PATH:/opt/cling/bin"
ENV CLING             "/opt/cling/bin/cling"
ENV CLING_COMMIT_SHA1 "/opt/cling/CLING_COMMIT_SHA1"

ENTRYPOINT "/opt/cling/bin/cling"
