ARG NODE_IMAGE=node:22-bookworm-slim@sha256:d649c27dae7ba0137b3cef5dd75baa422c08dc3d9e3fc0c23dfb172dc3cc6436
FROM ${NODE_IMAGE} AS build
WORKDIR /build
RUN npm install --global pnpm@11.21.0
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml tsconfig.json tsconfig.build.json ./
COPY site/package.json ./site/package.json
COPY src ./src
COPY scripts/clean-dist.mjs scripts/check-lfs-assets.mjs ./scripts/
COPY assets ./assets
COPY native ./native
COPY LICENSE README.md CHANGELOG.md CODE_OF_CONDUCT.md CONTRIBUTING.md SECURITY.md THIRD_PARTY_NOTICES.md ./
RUN pnpm install --frozen-lockfile --network-concurrency=4 \
    && pnpm build && node scripts/check-lfs-assets.mjs \
    && mkdir /out && npm pack --ignore-scripts --pack-destination /out \
    && rm -rf node_modules site/node_modules /root/.local/share/pnpm/store /root/.npm

FROM ${NODE_IMAGE} AS runtime
ENV SUITECUT_MEDIA_CACHE=/opt/suitecut-media
WORKDIR /app
COPY --from=build /out/suitecut-*.tgz /tmp/suitecut.tgz
RUN npm init --yes \
    && npm install --omit=dev --no-audit --no-fund /tmp/suitecut.tgz \
    && rm /tmp/suitecut.tgz && rm -rf /root/.npm \
    && ln -s node_modules/suitecut/dist dist \
    && mkdir -p .suitecut \
    && chown -R node:node /app \
    && rm -rf /var/lib/apt/lists/* /root/.npm
RUN apt-get update && apt-get install --yes --no-install-recommends procps xz-utils \
    && rm -rf /var/lib/apt/lists/*
RUN node node_modules/suitecut/dist/cli.js install
USER node
CMD ["node", "node_modules/suitecut/dist/cli.js", "doctor"]
