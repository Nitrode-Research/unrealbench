# Third-party material

All bundled license/notice files remain in their original task paths. The
generated RELEASE_REPORT.json lists the retained notice files so the release
review can check them without relying on this summary as an exhaustive inventory.

- NightSkyEngine source and bundled components/assets occur in the imported
  task workspaces. Retain their workspace LICENSE and component notices.
- GGPOUE4 includes its own LICENSE in the imported plugin source.
- Unreal Engine is an external dependency, obtained under Epic's terms; engine
  container images and engine binaries are not part of this source release.
- Harbor 0.23.0 and boto3 are installed through uv.lock, not vendored here.
  The lock retains boto3 from the existing tooling environment; local benchmark
  execution makes no AWS calls and requires no AWS credentials.
- The Ubuntu image used by the installation fixture is pulled independently.

The benchmark's top-level Apache-2.0 license covers Nitrode-owned code and task
content. Preserved third-party licenses and notices continue to apply to their
respective materials; the top-level license does not replace those terms.
