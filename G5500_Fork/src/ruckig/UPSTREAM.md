Ruckig Community 0.15.3

Source: https://github.com/pantor/ruckig/tree/v0.15.3
Archive SHA-256: 949c8b0b3227cf3f71a9750126b2952ba3a7af43e3be85505a853ebd17671166

MIT license in LICENSE. Cloud client, Python bindings and waypoint services are excluded.
Headers and C++ sources are colocated; ruckig include paths were changed to relative paths for Arduino sketch src compilation.
Local adaptation: TargetCalculator reuses one aligned workspace for Step 1 objects instead of placing their candidate profiles on the caller's stack. The numerical algorithms are unchanged. Each calculator owns its workspace; calculation remains allocation-free.
