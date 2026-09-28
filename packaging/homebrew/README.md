# Homebrew publishing contract

The end-user target is:

```bash
brew install <official-tap>/codecortex
codecortex-onboard
```

or, once the official tap owns post-install onboarding safely, a single `brew install` followed by the guided prompt.

This repository contains a formula template only. It is intentionally not presented as a live public tap until the official CodeCortex repository, release URLs and SHA-256 values are published.

The formula must install prebuilt release assets; it must not make normal users compile the product with CMake. Release automation should fill the template from the same signed/checksummed assets used by `scripts/install.sh`.
