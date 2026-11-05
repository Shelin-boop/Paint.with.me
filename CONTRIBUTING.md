# Contributing to Paint.with.me

Contributions are welcome: bug reports, ideas and code.

## Reporting a bug or an idea

Open an issue. For a bug, say what you did, what you expected and what happened,
and add the version from Help ▸ About and your Linux distribution. A sample image
helps a lot.

## Sending code

1. Fork the repository and create a branch.
2. Make your change. Keep it focused on one thing.
3. Build and run the tests:

   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   cmake --build build --target pwm_tests pwm_brush_tests pwm_paste_tests pwm_preview_tests pwm_integration_tests
   QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
   ```

4. Open a pull request. GitHub Actions builds it and runs the tests.

## Guidelines

- C++17 and Qt 6 Widgets. Qt 6.4 is the minimum.
- Interface text is in American English.
- New behavior comes with a test in `tests/`.
- Do not copy code from Paint.NET (closed source) or use its name, logo or icons
  in the product.
- By contributing you agree that your code is released under the MIT license.
