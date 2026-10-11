# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> explicit session retention is bounded and capacity returns after detach
- Location: benchmarks/native-engine/engine.e2e.spec.ts:470:1

# Error details

```
Error: browserContext.newPage: Protocol error (Browser.setWindowBounds): Window bounds exceed native capture dimensions
```