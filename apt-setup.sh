#!/usr/bin/env bash
# Build dependencies for the Viam C++ SDK. Runtime machines also need the
# shared gRPC and Boost libraries that libviamsdk links against.

apt-get install -qqy \
	build-essential \
	libabsl-dev \
	libboost-all-dev \
	libgrpc++-dev \
	libprotobuf-dev \
	pkg-config \
	ninja-build \
	protobuf-compiler-grpc \
	git \
	wget
