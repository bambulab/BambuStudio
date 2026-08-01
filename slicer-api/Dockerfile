FROM node:20-bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      curl git ca-certificates \
      libgtk-3-0t64 libglib2.0-0t64 libgl1 libegl1 \
      libgstreamer1.0-0 libgstreamer-gl1.0-0 \
      libsecret-1-0 libosmesa6 libcurl4t64 libdbus-1-3 \
      libglew2.2 libudev1 libmspack0 libxkbcommon0 \
      libunwind8 libfuse2t64 libwebkit2gtk-4.1-0 \
    && rm -rf /var/lib/apt/lists/*

ARG SLICER_API_REPO=https://github.com/maziggy/orca-slicer-api.git
ARG SLICER_API_BRANCH=bambuddy/profile-resolver
RUN git clone --branch ${SLICER_API_BRANCH} --depth 1 \
      ${SLICER_API_REPO} /app \
    && cd /app && npm ci --production \
    && apt-get purge -y git && apt-get autoremove -y \
    && rm -rf /var/lib/apt/lists/*

# Build artifacts are staged into slicer-api/stage/ by the CI workflow
# because .dockerignore excludes /build/.
COPY slicer-api/AppRun.sh /app/squashfs-root/AppRun
COPY slicer-api/stage/bin/bambu-studio /app/squashfs-root/bin/bambu-studio
COPY resources/ /app/squashfs-root/resources/
COPY slicer-api/stage/lib/ /app/squashfs-root/lib/

ENV LD_LIBRARY_PATH=/app/squashfs-root/lib

RUN chmod +x /app/squashfs-root/AppRun /app/squashfs-root/bin/bambu-studio

WORKDIR /app
ENV NODE_ENV=production
EXPOSE 3000

HEALTHCHECK --interval=30s --timeout=10s --retries=3 --start-period=30s \
  CMD curl -f http://localhost:3000/health || exit 1

CMD ["node", "server.js"]
