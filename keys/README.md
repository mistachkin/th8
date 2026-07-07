# `keys/`

This directory holds the **public-key halves** of the Harpy signing
trust anchors used by the signed-only loader at release time:

  * `keyRoot.snk` -- root trust anchor public key (RSA-16384).
  * `key0.snk`    -- primary signing subkey public key.
  * `keyTime.snk` -- short-lived time-bound signing subkey public key.

All three files are in Microsoft CAPI `PUBLICKEYBLOB` format
(magic `RSA1`).  They contain **only the public-key material**, not
the corresponding private keys.  They ship with the source release
so any downstream verifier can check signatures against the same
trust anchors the reference build uses.

The corresponding **private keys** live outside this repository
entirely.  They are held in the signing infrastructure used by the
authors of the reference release and are not redistributed.

## What ships

  * `keyRoot.snk`, `key0.snk`, `keyTime.snk` -- the three public-key
    blobs above.
  * The generated `.c` files containing these public keys baked in
    (`src/plugins/harpy/th8_key{Root,0,Time,Test}.c`), produced by
    `tools/mkkey.tcl`.
  * `tools/mkkey.tcl` -- the tool that regenerates the embedded
    `.c` files from the `.snk` files.

## What does NOT ship

  * Any private signing key (PEM-encoded RSA private keys, etc.).
    `.gitignore` excludes `keys/*.pem`, `keys/*.key`, and
    `keys/private*` so they cannot be checked in accidentally.

## Signing your own scripts during development

You do not need any of the production keys above to sign test
scripts.  Build with `ENABLE_TEST_KEY=1`:

```
make ENABLE_TEST_KEY=1 clean debug
```

That embeds a separate **test key pair** in the debug build.
Afterwards, `tools/signScript.sh tests/<file>.tcl` signs against
the test key, and the test suite (running under the debug build)
accepts the resulting signatures.

The test key is not one of the production keys; signatures it
produces are rejected by release builds.  That is intentional.

## Setting up your own production keys for a fork

If you fork TH8 and want to ship your own signed-only release with
your own trust anchors:

  1. Generate three RSA private keys (root + signing + time-bound)
     using your existing signing infrastructure.  TH8 expects
     RSA-2048 minimum; RSA-16384 is what the reference project uses
     for `keyRoot`.
  2. Export the public-key half of each in CAPI `PUBLICKEYBLOB`
     (magic `RSA1`) format and drop them in `keys/<name>.snk`.  The
     format is documented at the top of
     `src/plugins/harpy/th8_snk.c`.
  3. Run `tclsh tools/mkkey.tcl keys/<name>.snk` for each public-key
     file to regenerate the embedded `src/plugins/harpy/th8_<name>.c`.
  4. Keep the private halves of those keys somewhere safe and far
     away from this repository.  Re-run `make fresh`; signatures
     produced by your private keys will now verify against your
     public-key blobs in this directory.

Existing TH8 release binaries (built with the reference public-key
set above) will reject scripts signed by your private keys -- which
is exactly what you want, since a fork should have its own trust
anchors.
