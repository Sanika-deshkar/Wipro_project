# ============================================================================
#  Smart Vehicle Safety Monitoring and Dashboard System - Dockerfile
#  Multi-stage build for compiling and hosting the C backend on Render.
# ============================================================================

# Stage 1: Build the C executable using GCC and GNU Make
FROM gcc:bookworm AS builder

WORKDIR /app

# Copy build configuration and source code
COPY Makefile ./
COPY include/ ./include/
COPY src/ ./src/

# Compile the application using the existing Makefile (generates build/svsm)
RUN make all

# Stage 2: Minimal runtime image
FROM debian:bookworm-slim

WORKDIR /app

# Install certificates and clean up cache
RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# Copy the compiled C binary from the builder stage
COPY --from=builder /app/build/svsm /app/svsm
RUN chmod +x /app/svsm

# Copy static frontend assets (allows the C backend to also serve UI directly)
COPY web/ /app/web/

# Ensure logs directory exists for persisting alert history
RUN mkdir -p /app/logs

# Render dynamically assigns the PORT environment variable (defaults to 8080)
ENV PORT=8080

EXPOSE 8080

# Launch the C backend server:
# --serve ${PORT}: listens on Render's assigned port
# --public: binds to 0.0.0.0 (all interfaces) so Render router can reach the service
# --web web: serves static files from web/
CMD ["sh", "-c", "./svsm --serve ${PORT:-8080} --public --web web"]
