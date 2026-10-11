# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> repeated context/page shutdown keeps the engine usable
- Location: benchmarks/native-engine/engine.e2e.spec.ts:423:1

# Error details

```
Error: browserContext.newPage: Protocol error (Browser.setWindowBounds): Window bounds exceed native capture dimensions
```