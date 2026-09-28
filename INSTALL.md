# Installing CodeCortex

After the CodeCortex binary is installed, the normal user setup is one command:

```bash
codecortex install
```

It auto-detects supported coding CLIs and wires CodeCortex in. No CMake, manual MCP JSON editing, intent classifier, or agent framework is required for normal use. The core MCP and supported Codex/Claude observation hooks use the native CodeCortex binary. Python is not required for normal runtime or hook observation.

Target one host when desired:

```bash
codecortex install --host codex
codecortex install --host claude
codecortex install --host gemini
```

## End users

Use a prebuilt CodeCortex release/package. The public installer should install the binary; normal users should not need CMake or a source checkout.

After installation, run:

```bash
codecortex install
```

The unified flow detects supported hosts and registers the local `codecortex` MCP. For an unrecognized MCP-compatible client, run `codecortex install --generic` and use the printed stdio or loopback HTTP recipe.

After setup, open the coding tool on any Git repository and work normally. Ask **"Show CodeCortex status"** to display the active project/run and dashboard endpoint.

## Source build for developers

Requirements: CMake and a C++23 toolchain.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

Run focused tests for the areas you change and the repository hygiene/licensing gates before publishing a source package.

## Generic MCP clients

For another CLI, IDE, or desktop application that supports MCP:

```bash
codecortex install --generic
```

Recommended configuration:

```text
transport: stdio
command: codecortex
args: [".", "--mcp", "--mcp-ui"]
```

If the client requires HTTP, keep CodeCortex local:

```bash
codecortex . --mcp --listen=127.0.0.1:7332 --mcp-ui
```

Use `http://127.0.0.1:7332/mcp` as the MCP endpoint. Non-loopback HTTP is an advanced deployment; see `docs/DEPLOYMENT_ARCHITECTURE.md`.

## Upgrading from pre-0.7 builds
Run `codecortex install` after installing the new binary. The installer removes retired MCP registrations from detected hosts and registers the canonical `codecortex` server. It intentionally leaves any retired executable on disk so migration is reversible.

For a dashboard that stays up independently of an MCP host session, run:

```bash
codecortex dashboard /path/to/repository
```

The dashboard remains loopback-only at `http://127.0.0.1:7331`.
