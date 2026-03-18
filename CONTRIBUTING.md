# Contributing

Thank you for your interest in contributing.

This document outlines the guidelines and requirements for contributing to this project. Following these rules helps maintain consistency, quality, and stability.

---

## Workflow

1. Fork the repository
2. Create a new branch:
   - `feature/your-feature`
   - `fix/your-bug`
3. Make your changes
4. Commit with clear and descriptive messages
5. Push to your fork
6. Open a Pull Request

---

## Code Style

All contributions must follow the project's coding standards:

- Use **tabs** for indentation
- Follow the **C++20** standard
- Apply **LLVM-style formatting**
- Ensure compatibility with **MSVC 2022**
- Write documentation using **Doxygen-style comments**

### Formatting

This project uses:

- `.editorconfig` for basic formatting rules
- `.clang-format` for C++ code formatting

Make sure your editor respects these configurations.

---

## Build Requirements

Before submitting a pull request, ensure that:

- The project builds successfully using **MSVC 2022**
- The build works in **Release configuration**

---

## Continuous Integration

All pull requests are automatically validated.

Checks include:

- Compliance with `.editorconfig`
- Successful compilation using MSVC (C++20, Release)

Pull requests must pass all checks before they can be merged.

---

## Pull Request Guidelines

- Keep changes **small and focused**
- Avoid unrelated modifications in the same PR
- Provide a clear description of what was changed and why
- Ensure your changes do not break existing functionality

---

## Issues

When opening an issue, include:

- Steps to reproduce (if applicable)
- Expected behavior
- Actual behavior

---

## Code of Conduct

By contributing, you agree to follow the project's Code of Conduct.
