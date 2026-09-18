FROM ubuntu:26.04@sha256:513c074113a871b51a8d16ab445c88779d6452d937a164fb5cc479f32668a41d
RUN useradd --create-home ue4
COPY --chown=ue4:ue4 workspace/ /project/
WORKDIR /project
USER ue4
