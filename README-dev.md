# Developer Guide

## Local setup

```bash
git submodule update --init --recursive   # pulls seedsigner-c-modules + LVGL
```

## Build

```bash
./run_build.sh
```

This produces an ARMv6 CPython extension (`.so`) targeting the Pi Zero. The
build runs inside a Docker container under QEMU ARM emulation. Expect ~13
minutes for the first build; subsequent builds with a warm ccache complete
in ~1 minute.

## Pi hardware testing

See `docs/pi-hardware-test.md`.

---

## Build details

The build uses a pre-built base image containing a pinned Python toolchain and
ARMv6 compiler, ensuring reproducible builds across local dev machines and CI.
The image is mirrored to two registries: **GHCR**
(`ghcr.io/kdmukai-bot/...`), pulled by GitHub Actions, and the **GitLab
container registry** (`registry.gitlab.com/kdmukai-bot/...`), pulled by GitLab
CI, Forgejo CI, and local `run_build.sh`. It is built and published **locally**
(see "Rebuilding and publishing the base image" below).

### Build caching

Local builds use Docker named volumes to persist caches across runs:

- **ccache** (`seedsigner-raspi-lvgl-ccache`) — compiled object cache, avoids recompiling unchanged translation units under QEMU emulation
- **venv** (`seedsigner-raspi-lvgl-venv`) — Python virtual environment with build dependencies

To reset caches:
```bash
docker volume rm seedsigner-raspi-lvgl-ccache seedsigner-raspi-lvgl-venv
```

CI builds use `actions/cache` for ccache persistence across workflow runs.

### Build logs

All builds write timestamped logs to `logs/`.

### Rebuilding and publishing the base image

The base image (definition: `docker/Dockerfile`) is built and published
locally rather than by an automated CI job: publishing requires a registry token
with write access, and keeping that token off CI runners avoids exposing it to a
compromised build-time dependency that could exfiltrate it. The image changes
rarely — only on a Python or system-toolchain bump — so a manual local publish is
a worthwhile trade. Publish to all three registries so every consumer (and
mirror) stays in sync:

| Registry | Pulled by |
|----------|-----------|
| `ghcr.io/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev` | GitHub Actions (`.github/workflows/build.yml`) |
| `registry.gitlab.com/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev` | GitLab CI, Forgejo CI, local `run_build.sh` |
| `codeberg.org/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev` | mirror (kept in sync across the bot's forges) |

**Creating the publish tokens (one-time).** You need one token per registry,
each scoped to *registry write only* and with a **short expiry** — they're used
rarely, so regenerate on demand rather than keeping a long-lived token around.
Store them in your password manager, never on disk. Create both as the
`kdmukAI-bot` account.

*GHCR — classic PAT:*
1. GitHub → **Settings → Developer settings → Personal access tokens → Tokens
   (classic) → Generate new token (classic)**.
2. Name it (e.g. `ghcr-base-image-publish`); set a short **Expiration** (e.g. 7 days).
3. Under **Select scopes**, check **`write:packages`** (this implies
   `read:packages`). Nothing else is needed to push to a package the account owns.
4. **Generate token** → copy the `ghp_…` value into your password manager.
   - Use a **classic** PAT — fine-grained PATs are unreliable for GHCR, and the
     token must belong to `kdmukAI-bot` (the package owner).

*GitLab — Personal Access Token:*
1. GitLab → avatar → **Edit profile → Access Tokens**
   (`gitlab.com/-/user_settings/personal_access_tokens`).
2. Name it; set an **Expiration date**.
3. Scopes: check **`write_registry`** and **`read_registry`** only.
4. **Create** → copy the `glpat-…` value into your password manager.
   - More-scoped alternative: a **project deploy token** (project → **Settings →
     Repository → Deploy tokens**) with `read_registry` + `write_registry`; it
     issues its own username + token to use at the `docker login` prompt.

*Codeberg — access token (Forgejo permission-scoped):*
1. Codeberg → **Settings → Applications → Access Tokens**
   (`codeberg.org/user/settings/applications`).
2. Name it.
3. Set **Repository and organization access** to **"Public only"** — this field is
   required (it cannot be empty). Choose "Public only", not "All (public, private
   and limited)"; public-only is least-privilege and does not gate the registry push.
4. Set the **`package`** permission to **Read and write** (this is what authorizes
   the push); leave the other permission scopes at **No Access**.
5. **Generate Token** → copy the value into your password manager.
6. Codeberg tokens have no expiry field, so **delete this token right after
   publishing** (same page) to keep it short-lived.

**1. Build locally** (produces `…/python-armv6:py310-dev-local`):
```bash
PYTHON_VERSION=3.10.10 PY_SERIES=py310-dev ./docker/build_base_image.sh
```

**2. Tag for all registries:**
```bash
LOCAL=seedsigner-raspi-lvgl/python-armv6:py310-dev-local
docker tag "$LOCAL" ghcr.io/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev
docker tag "$LOCAL" registry.gitlab.com/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev
docker tag "$LOCAL" codeberg.org/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev
```

**3. Log in and push — without leaving a token on disk or in shell history.**
Pull each token from your password manager (GHCR: a classic PAT with
`write:packages`; GitLab: a PAT or deploy token with registry write; Codeberg: an
access token with `write:package`). Paste it at
a hidden prompt and use a throwaway Docker config so nothing persists in
`~/.docker/config.json`:
```bash
export DOCKER_CONFIG="$(mktemp -d)"          # throwaway, not ~/.docker

# --- GHCR ---
read -rs TOKEN && echo                        # paste GHCR PAT; hidden, not in history
printf '%s' "$TOKEN" | docker login ghcr.io -u kdmukAI-bot --password-stdin
docker push ghcr.io/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev
docker logout ghcr.io

# --- GitLab ---
read -rs TOKEN && echo                        # paste GitLab token
printf '%s' "$TOKEN" | docker login registry.gitlab.com -u kdmukAI-bot --password-stdin
docker push registry.gitlab.com/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev
docker logout registry.gitlab.com

# --- Codeberg ---
read -rs TOKEN && echo                        # paste Codeberg token
printf '%s' "$TOKEN" | docker login codeberg.org -u kdmukAI-bot --password-stdin
docker push codeberg.org/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py310-dev
docker logout codeberg.org

rm -rf "$DOCKER_CONFIG"; unset DOCKER_CONFIG TOKEN
```

> **Never** `echo "<token>" | docker login …` — that writes the literal token
> into `~/.bash_history`. Always read it into a variable via `read -rs` (hidden,
> not recorded) and pipe with `--password-stdin`.

### Submodules

The repo includes `seedsigner-c-modules` as a git submodule under
`sources/seedsigner-c-modules`, which itself contains LVGL as a nested
submodule (`third_party/lvgl`). The `--recursive` flag is required.

### Environment variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `IMAGE_TAG` | GHCR base image for ARMv6 builds | `py310-dev` tag |
| `LVGL_PERF_MONITOR` | Enable LVGL FPS/CPU overlay | `0` |
| `SEEDSIGNER_C_MODULES_DIR` | Path to c-modules source | `sources/seedsigner-c-modules` |
| `LVGL_ROOT` | Path to LVGL source | `sources/seedsigner-c-modules/third_party/lvgl` |
| `WS_ROOT` | Workspace root for Docker mounts | auto-detected |
| `LOCK_FILE` | Version lock file | `versions.lock.toml` |
| `ABI_JSON` | ABI reference file | `docs/abi/dev-pi-abi.json` |
| `CCACHE_HOST_DIR` | Host path for ccache (CI use) | Docker named volume |

#### Examples

Enable LVGL performance monitor overlay (displays FPS/CPU on screen):
```bash
LVGL_PERF_MONITOR=1 ./run_build.sh
```

Use a locally-built base image instead of the GHCR image:
```bash
IMAGE_TAG=seedsigner-raspi-lvgl/python-armv6:py310-dev-local ./run_build.sh
```
