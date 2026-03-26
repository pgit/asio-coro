# Workspace Instructions

## Testing
- Prefer GoogleTest binaries over CTest in this repo.
- Default to running `build/test/test_all` with `--gtest_filter=<TestName>` when asked to run a specific test.
- Use CTest only if the user explicitly requests it or when GoogleTest binaries are unavailable.
