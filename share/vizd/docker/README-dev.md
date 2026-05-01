# VIZ Developer Docker Image

`Dockerfile-dev` builds a long-lived Linux toolchain image that mirrors the CI
builder stage (same base image and packages as `Dockerfile-production`) without
baking any source code in. Mount the worktree as a volume and compile/test
inside the container against a real Ubuntu Noble environment.

## Prerequisites

- Docker Desktop (or Docker Engine) running locally.
- Git submodules initialized on the host **before** using the container:

  ```bash
  git submodule update --init --recursive
  ```

## Build the image

Run once from the repository root (or worktree root):

```bash
docker build -f share/vizd/docker/Dockerfile-dev -t viz-dev .
```

Typical build time: 3–8 minutes on first run (pulls base image + ~30 apt
packages). Subsequent builds are fast thanks to Docker layer cache.

## Enter an interactive shell

```bash
docker run --rm -it -v $(pwd):/workspace viz-dev
```

Inside the container your worktree is at `/workspace`. Run cmake, make, ctest,
gcovr, or gdb from there.

## Run a one-shot command

```bash
docker run --rm -v $(pwd):/workspace viz-dev bash -c "
  mkdir -p build-docker && cd build-docker
  cmake -DCMAKE_BUILD_TYPE=Release -DLOW_MEMORY_NODE=OFF ..
  make -j$(nproc) vizd
"
```

## Configure only (no build)

```bash
docker run --rm -v $(pwd):/workspace viz-dev bash -c "
  mkdir -p build-docker && cd build-docker
  cmake -DCMAKE_BUILD_TYPE=Release -DLOW_MEMORY_NODE=OFF .. 2>&1 | tail -30
"
```

## ccache

The image sets `CCACHE_DIR=/workspace/.ccache`. When you mount the worktree
with `-v $(pwd):/workspace`, ccache writes to `<worktree>/.ccache/` on the
host and persists across container runs. The `.ccache/` directory is listed in
`.git/info/exclude` so it won't appear in `git status`.

## Clean up

Remove the build directory created inside the worktree:

```bash
rm -rf build-docker/
```

Remove the image:

```bash
docker rmi viz-dev
```
