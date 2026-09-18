FROM ghcr.io/epicgames/unreal-engine:dev-slim-5.8.2@sha256:6840106f40dfa6655d6959838aee0006f1bd693cac442ecd7753a9dbf267d649
ENV UV_PYTHON=3.11
COPY --chown=ue4:ue4 workspace/ /project/
COPY --chown=ue4:ue4 protected/ /protected-baseline/
WORKDIR /project
