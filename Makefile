# Hypervisor - Distributed Cloud Operating System
# Makefile for Go + C mixed project

.PHONY: all build build-server build-agent build-ctl
.PHONY: proto proto-gen proto-deps
.PHONY: clib test lint clean docker deps
.PHONY: run-server run-agent

# ============================================================================
# Variables
# ============================================================================

GO := go
GOPATH := $(shell go env GOPATH)
GOOS ?= $(shell go env GOOS)
GOARCH ?= $(shell go env GOARCH)

# Ensure GOPATH/bin is in PATH for protoc plugins
export PATH := $(PATH):$(GOPATH)/bin
CGO_ENABLED := 1
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
BUILD_TIME := $(shell date -u +"%Y-%m-%dT%H:%M:%SZ")
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")

LDFLAGS := -ldflags "\
	-X hypervisor/internal/version.Version=$(VERSION) \
	-X hypervisor/internal/version.BuildTime=$(BUILD_TIME) \
	-X hypervisor/internal/version.GitCommit=$(GIT_COMMIT)"

# C Compiler settings
CC := gcc
CFLAGS := -fPIC -O2 -Wall -Wextra
LIBVIRT_CFLAGS := $(shell pkg-config --cflags libvirt 2>/dev/null)
LIBVIRT_LIBS := $(shell pkg-config --libs libvirt 2>/dev/null)

# Directories
BIN_DIR := bin
CLIB_DIR := clib
PROTO_DIR := api/proto
GEN_DIR := api/gen

# ============================================================================
# Default target
# ============================================================================

all: deps proto build

# Build with libvirt support (requires libvirt-dev)
all-with-libvirt: deps proto clib build

# ============================================================================
# Dependencies
# ============================================================================

deps:
	$(GO) mod tidy
	$(GO) mod download

proto-deps:
	@which protoc > /dev/null || (echo "protoc not found. Install protobuf compiler." && exit 1)
	@which protoc-gen-go > /dev/null || $(GO) install google.golang.org/protobuf/cmd/protoc-gen-go@latest
	@which protoc-gen-go-grpc > /dev/null || $(GO) install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest

# ============================================================================
# Protocol Buffers
# ============================================================================

proto: proto-deps proto-gen

proto-gen:
	@mkdir -p $(GEN_DIR)
	protoc \
		--proto_path=$(PROTO_DIR) \
		--go_out=$(GEN_DIR) \
		--go_opt=paths=source_relative \
		--go-grpc_out=$(GEN_DIR) \
		--go-grpc_opt=paths=source_relative \
		$(PROTO_DIR)/*.proto

# ============================================================================
# C Library
# ============================================================================

clib: $(CLIB_DIR)/libvirt-wrapper/liblvwrapper.a

$(CLIB_DIR)/libvirt-wrapper/liblvwrapper.a: $(CLIB_DIR)/libvirt-wrapper/libvirt_wrapper.c
	$(CC) $(CFLAGS) $(LIBVIRT_CFLAGS) -c $< -o $(CLIB_DIR)/libvirt-wrapper/libvirt_wrapper.o
	ar rcs $@ $(CLIB_DIR)/libvirt-wrapper/libvirt_wrapper.o

clib-clean:
	rm -f $(CLIB_DIR)/libvirt-wrapper/*.o $(CLIB_DIR)/libvirt-wrapper/*.a

# ============================================================================
# Build
# ============================================================================

build: build-server build-agent build-ctl

build-server:
	@mkdir -p $(BIN_DIR)
	CGO_ENABLED=$(CGO_ENABLED) GOOS=$(GOOS) GOARCH=$(GOARCH) \
		$(GO) build $(LDFLAGS) -o $(BIN_DIR)/hypervisor-server ./cmd/hypervisor-server

build-agent:
	@mkdir -p $(BIN_DIR)
	CGO_ENABLED=$(CGO_ENABLED) GOOS=$(GOOS) GOARCH=$(GOARCH) \
		$(GO) build $(LDFLAGS) -o $(BIN_DIR)/hypervisor-agent ./cmd/hypervisor-agent

build-ctl:
	@mkdir -p $(BIN_DIR)
	CGO_ENABLED=0 GOOS=$(GOOS) GOARCH=$(GOARCH) \
		$(GO) build $(LDFLAGS) -o $(BIN_DIR)/hypervisor-ctl ./cmd/hypervisor-ctl

# Cross-compile for Linux (useful for macOS development)
build-linux:
	GOOS=linux GOARCH=amd64 $(MAKE) build

# ============================================================================
# Run
# ============================================================================

run-server:
	$(GO) run ./cmd/hypervisor-server --config configs/server.yaml

run-agent:
	$(GO) run ./cmd/hypervisor-agent --config configs/agent.yaml

# ============================================================================
# Testing
# ============================================================================

test:
	$(GO) test -v -race -cover ./...

test-short:
	$(GO) test -v -short ./...

test-coverage:
	$(GO) test -v -race -coverprofile=coverage.out ./...
	$(GO) tool cover -html=coverage.out -o coverage.html

# ============================================================================
# Linting
# ============================================================================

lint:
	@which golangci-lint > /dev/null || (echo "golangci-lint not found" && exit 1)
	golangci-lint run ./...

fmt:
	$(GO) fmt ./...
	gofmt -s -w .

vet:
	$(GO) vet ./...

# ============================================================================
# Docker
# ============================================================================

docker: docker-server docker-agent

docker-server:
	docker build -t hypervisor-server:$(VERSION) -f deployments/docker/Dockerfile.server .

docker-agent:
	docker build -t hypervisor-agent:$(VERSION) -f deployments/docker/Dockerfile.agent .

# ============================================================================
# Clean
# ============================================================================

clean: clib-clean
	rm -rf $(BIN_DIR)
	rm -rf $(GEN_DIR)
	rm -f coverage.out coverage.html

# ============================================================================
# Help
# ============================================================================

help:
	@echo "Hypervisor - Distributed Cloud Operating System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all           Build everything (deps, proto, clib, build)"
	@echo "  build         Build all binaries"
	@echo "  build-server  Build hypervisor-server"
	@echo "  build-agent   Build hypervisor-agent"
	@echo "  build-ctl     Build hypervisor-ctl"
	@echo "  build-linux   Cross-compile for Linux"
	@echo "  proto         Generate protobuf code"
	@echo "  clib          Build C libraries"
	@echo "  deps          Download Go dependencies"
	@echo "  test          Run tests with race detection"
	@echo "  lint          Run golangci-lint"
	@echo "  docker        Build Docker images"
	@echo "  clean         Clean build artifacts"
	@echo "  help          Show this help message"
