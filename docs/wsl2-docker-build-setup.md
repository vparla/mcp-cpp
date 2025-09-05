# Windows 11: Minimal WSL2 + Docker setup for C++ build containers (multi-arch capable)

This guide installs the smallest practical stack to run Docker containers for C++ builds on Windows 11, with optional multi-architecture (linux/amd64 + linux/arm64) support. Two supported setups are provided:

- Recommended: Docker Desktop with WSL 2 backend (simple, integrated, supports Buildx)
- Alternative: Docker Engine inside a WSL 2 Linux distro (no Docker Desktop)

Either option lets you run build containers. Use Buildx + QEMU to build/publish multi-arch images.

---

## 0) Prerequisites

- Windows 11 with virtualization enabled in BIOS/UEFI.
- Administrator PowerShell for setup commands.

---

## 1) Install or update WSL 2

Run in an elevated PowerShell:

```powershell
wsl --install
wsl --set-default-version 2
wsl --update
# optional: pick a distro now (Ubuntu is recommended)
wsl --install -d Ubuntu
```

- If WSL is already installed, `wsl --update` brings kernel/userspace up to date.
- Launch Ubuntu once to complete its first-time setup (create username/password).

---

## Option A (Recommended): Docker Desktop + WSL 2 backend

> Important: Docker Desktop is a Windows application. Install it on the Windows host (not inside WSL). After installation, enable WSL Integration from Docker Desktop on Windows (Settings → Resources → WSL Integration) so your Ubuntu distro can use the Windows Docker engine. If you want everything inside WSL without Docker Desktop, skip Option A and use Option B to install Docker Engine inside your WSL Ubuntu distro.

Note: After WSL integration is enabled, you can run Docker commands from either Windows PowerShell or your WSL Ubuntu shell.

1) Install Docker Desktop
- Download: https://www.docker.com/products/docker-desktop/
- During install, keep “Use the WSL 2 based engine” enabled.

2) Enable WSL integration for your distro
- Docker Desktop → Settings → Resources → WSL Integration → enable your distro (e.g., Ubuntu).

3) Verify Docker works

```powershell
docker version
docker run --rm hello-world
```

4) Enable multi-arch builds (Buildx + QEMU/binfmt)

```powershell
# Buildx is bundled with Docker Desktop
docker buildx version

# Ensure QEMU/binfmt is installed for cross-arch building/running
docker run --privileged --rm tonistiigi/binfmt --install all

# Create/activate a builder and bootstrap it
docker buildx create --name cross --use
docker buildx inspect --bootstrap
```

5) Build examples

- Multi-arch Linux image (push to registry):

```powershell
docker buildx build --platform linux/amd64,linux/arm64 -t <registry>/<repo>:<tag> --push .
```

- Export multi-arch build artifacts locally (no push):

```powershell
# saves build outputs under ./out/<platform>/...
docker buildx build --platform linux/amd64,linux/arm64 -o type=local,dest=out .
```

- Sanity test a build container for C++ tools (runs locally):

```powershell
# Uses Ubuntu as a throwaway build shell; verifies compilers can run
docker run --rm -v ${PWD}:/src -w /src ubuntu:22.04 bash -lc "apt-get update && apt-get install -y build-essential cmake ninja-build && g++ --version && cmake --version && ninja --version"
```

Notes:
- If you need Windows container images (for MSVC-based builds), switch Docker Desktop to Windows containers (tray icon → “Switch to Windows containers”). Linux and Windows images require different engines.

---

## Option B (No Docker Desktop): Docker Engine inside WSL 2 (Ubuntu)

All Docker commands will run inside the WSL Ubuntu shell.

1) Install Docker Engine

```bash
# inside Ubuntu (WSL)
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg

sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo $VERSION_CODENAME) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

2) (Recommended) Enable systemd in WSL for services

```bash
# inside Ubuntu
sudo bash -lc 'cat >/etc/wsl.conf <<\EOF
[boot]
systemd=true
EOF'
```

Then from Windows PowerShell:

```powershell
wsl --shutdown
```

Reopen Ubuntu; Docker should start via systemd. If not using systemd, start manually:

```bash
sudo service docker start
```

3) Allow your user to run docker without sudo

```bash
sudo usermod -aG docker $USER
# log out of the WSL session and back in
```

### Quick fix: "permission denied" on /var/run/docker.sock (inside WSL)

If `docker run --rm hello-world` fails with a permission error:

- Check/add docker group and apply membership:

```bash
id -nG
sudo groupadd docker 2>/dev/null || true
sudo usermod -aG docker $USER
# Apply new group without reopening terminal (starts a subshell)
newgrp docker
```

- Ensure the Docker daemon is running:

```bash
# if you enabled systemd
sudo systemctl status docker || sudo systemctl start docker

# if not using systemd
sudo service docker status || sudo service docker start
```

- Verify/fix socket ownership and permissions:

```bash
ls -l /var/run/docker.sock
sudo chown root:docker /var/run/docker.sock
sudo chmod 660 /var/run/docker.sock
```

- If you recently enabled systemd in `/etc/wsl.conf`, restart WSL from Windows and reopen Ubuntu:

```powershell
wsl --shutdown
```

- If you are using Docker Desktop (Option A), ensure WSL Integration is enabled in the Windows UI for your Ubuntu distro (Settings → Resources → WSL Integration), then retry.

4) Verify and enable multi-arch

```bash
docker version
docker run --rm hello-world

# QEMU/binfmt for cross-arch builds
docker run --privileged --rm tonistiigi/binfmt --install all

docker buildx create --name cross --use
docker buildx inspect --bootstrap
```

5) Build examples (inside WSL)

```bash
# Push a multi-arch image
docker buildx build --platform linux/amd64,linux/arm64 -t <registry>/<repo>:<tag> --push .

# Export artifacts locally (to ./out)
docker buildx build --platform linux/amd64,linux/arm64 -o type=local,dest=out .
```

Note:
- This setup builds Linux images only. Windows images require Windows containers (Docker Desktop) or cross-compiling to Windows via MinGW in a Linux container.

---

## C++ specifics (quick guidance)

- Linux amd64/arm64 builds: Use Buildx; in Dockerfiles leverage `$BUILDPLATFORM` and `$TARGETARCH` to configure toolchains.
- Windows artifacts:
  - If MSVC is required, build with Windows containers or native Windows toolchain.
  - If GNU toolchain is acceptable, cross-compile from Linux with MinGW-w64 inside a container.
- macOS artifacts: Prefer macOS CI runners for legal access to Apple SDKs and codesigning; osxcross-in-Docker is possible but requires you to supply the SDK from a Mac and has limitations.

Minimal cross-compile toolchain snippets (for reference):

- Linux arm64 with CMake (in a Linux build container):

```cmake
# toolchain-aarch64.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
```

- Windows (MinGW) with CMake:

```cmake
# toolchain-mingw.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
```

---

## Troubleshooting

- Virtualization disabled: Enable Intel VT-x/AMD-V and VT-d/IOMMU in BIOS/UEFI.
- WSL kernel out of date: `wsl --update` (PowerShell as Admin).
- Docker cannot talk to the daemon:
  - Docker Desktop: ensure the app is running; check WSL Integration.
  - WSL Engine: `sudo service docker status` (or enable systemd).
- Buildx shows limited platforms: rerun `docker run --privileged --rm tonistiigi/binfmt --install all` and `docker buildx inspect --bootstrap`.
- Multi-arch `--load` limitation: Only single-platform outputs can be `--load`ed; multi-arch builds should use `--push` or `-o` exporters.

---

## Quick command checklist

```powershell
# WSL 2
wsl --install
wsl --set-default-version 2
wsl --update
wsl --install -d Ubuntu

# Docker Desktop (after install)
docker run --privileged --rm tonistiigi/binfmt --install all
docker buildx create --name cross --use
docker buildx inspect --bootstrap

# Multi-arch build (push)
docker buildx build --platform linux/amd64,linux/arm64 -t <registry>/<repo>:<tag> --push .

# Export artifacts locally
docker buildx build --platform linux/amd64,linux/arm64 -o type=local,dest=out .
```

---

That’s it. Use Option A for the simplest experience; use Option B if you prefer an OSS-only stack without Docker Desktop. Both support running build containers and multi-arch Linux builds on Windows 11.
