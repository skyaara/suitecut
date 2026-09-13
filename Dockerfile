ARG NODE_IMAGE=node:22-bookworm-slim@sha256:d649c27dae7ba0137b3cef5dd75baa422c08dc3d9e3fc0c23dfb172dc3cc6436
FROM ${NODE_IMAGE} AS build
WORKDIR /build
RUN npm install --global pnpm@11.21.0
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml tsconfig.json tsconfig.build.json ./
COPY site/package.json ./site/package.json
COPY plugins ./plugins
COPY src ./src
COPY scripts/clean-dist.mjs ./scripts/clean-dist.mjs
COPY assets ./assets
COPY LICENSE README.md CHANGELOG.md CODE_OF_CONDUCT.md CONTRIBUTING.md SECURITY.md THIRD_PARTY_NOTICES.md ./
RUN pnpm install --frozen-lockfile --network-concurrency=4 \
    && pnpm build && mkdir /out && npm pack --ignore-scripts --pack-destination /out \
    && rm -rf node_modules site/node_modules plugins/*/node_modules /root/.local/share/pnpm/store /root/.npm

FROM ${NODE_IMAGE} AS runtime
ENV PLAYWRIGHT_BROWSERS_PATH=/opt/playwright
ENV SUITECUT_MEDIA_CACHE=/opt/suitecut-media
WORKDIR /app
COPY --from=build /out/suitecut-*.tgz /tmp/suitecut.tgz
RUN npm init --yes \
    && npm install --omit=dev --no-audit --no-fund /tmp/suitecut.tgz playwright@1.62.1 \
    && rm /tmp/suitecut.tgz && rm -rf /root/.npm \
    && ln -s node_modules/suitecut/dist dist \
    && mkdir -p .suitecut \
    && chown -R node:node /app \
    && rm -rf /var/lib/apt/lists/* /root/.npm
RUN npx playwright install-deps chromium && apt-get install --yes --no-install-recommends procps xz-utils \
    && rm -rf /var/lib/apt/lists/*
RUN npx playwright install chromium
RUN node node_modules/suitecut/dist/cli.js install
COPY scripts/verify-live-stream.mjs scripts/verify-tab-audio.mjs ./scripts/
USER node
CMD ["node", "node_modules/suitecut/dist/cli.js", "doctor"]
