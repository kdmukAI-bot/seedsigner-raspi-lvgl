# Remaining work: production-grade SDK reproducibility

**Status:** SCOPE — deferred until the production-release push.

CI cross-compiles against the SeedSigner OS buildroot SDK; the mechanism and its
constraints are documented in `docs/knowledge/armv6-cross-compile-sdk.md`. What is
**not** yet in place is a reproducible, automated path from SeedSigner OS *source*
to the published SDK image.

## The gap

`docker/build_sdk_image.sh` extracts `/output/host` from an existing buildroot
output (a build container or a host tree) and stamps the image with the
seedsigner-os commit it came from. So the provenance claim — "this artifact was
built against SeedSigner OS `<commit>`" — is recorded and independently checkable
by rebuilding the OS at that commit.

But the image itself is a **capture of a binary tree someone already built**, not a
product of a pinned from-source build. Two consequences:

- Nothing mechanically guarantees the extracted tree corresponds to the commit
  stamped on it. A dirty or stale buildroot output produces a confidently
  mislabelled SDK. (The producer warns on a dirty seedsigner-os checkout; it cannot
  detect a stale `/output`.)
- Rebuilding the SDK from scratch is a manual, undocumented-by-automation act.

That is acceptable for development. It is not acceptable for a release whose
supply chain must be auditable.

## Target state

1. **From-source producer.** A committed workflow that, given a seedsigner-os ref,
   runs that project's own buildroot build (it is already a from-source,
   Docker-based, pinned build: `debian:12` + the `opt/buildroot` submodule) and
   packages the resulting `output/host` into the SDK image. The image then *is* a
   function of `(seedsigner-os ref x buildroot submodule ref)`.
2. **Published, versioned, immutable.** The SDK pushed to a public registry, tagged
   to the OS release. CI pins that tag; bumping the OS = rebuild SDK + move the tag
   in one commit. This is the automation whose absence let the toolchain silently
   rot before.
3. **Verifiable correspondence.** Ideally a digest of the extracted sysroot recorded
   at build time, so an auditor can confirm a rebuild reproduces the same toolchain
   and libraries rather than trusting the tag.

## Open decisions

- **Where the from-source build runs.** A buildroot build is tens of minutes to
  hours; it cannot sit on the per-PR path. Options: a manually dispatched workflow,
  or one triggered on a seedsigner-os release.
- **Whether buildroot output is bit-reproducible** across rebuilds of the same ref.
  If not, "verifiable correspondence" weakens to ABI-equivalence checks rather than
  digest equality, and the acceptance bar should say so explicitly.
- **Folding into the buildroot external-package end state.** The long-term
  direction is building this extension as a Buildroot external package inside
  seedsigner-os (see the raspi-lvgl / seedsigner-os relationship notes). That would
  subsume the SDK image entirely. Worth deciding whether to invest in a hardened
  standalone SDK producer first, or jump straight to the external package.

## Also open (independent of reproducibility)

- **CI runs no functional tests.** All 64 native tests skip on the x86 build host by
  construction. A green build certifies compile + ABI/ELF conformance only. Either
  accept this (functional gating stays on-device), add an optional `qemu-user` import
  smoke off the critical path, or make the vacuity explicit by failing when zero
  tests actually execute.
