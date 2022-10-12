# This builds a very small image able to build docker images.
FROM alpine:latest
RUN apk add --update docker openrc bash
RUN rc-update add docker boot
