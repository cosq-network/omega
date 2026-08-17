# Workflow

- Wants implementation done "carefully and optimistically," without breaking any existing features. Confidence: 0.98
- Wants changes verified with unit tests and integration tests whenever possible, including per-architecture coverage for x86_64, AArch64, and RISC-V. Confidence: 0.95
- Prefers a detailed plan document written into docs/ before starting a large implementation, then implementing that plan. Confidence: 0.95
- Scopes plans for new components to cover shipping/distribution — packaging into the bootable image, CI lanes, and licensing — not just implementation ("integrate and ship"). Confidence: 0.6
- Wants documentation (README.md, docs/ROADMAP.md, and any related plan docs) updated to reflect every round of changes. Confidence: 0.9
- Wants detailed, consolidated git commit messages covering all changes made in the session. Confidence: 0.9
- Prefers to grant blanket approval for common build commands (e.g., anything starting with cmake) and wants the agent to proceed autonomously without asking permission for each change. Confidence: 0.85
- Works in an iterative roadmap-driven loop: review ROADMAP, implement the next items, verify, commit, then ask "what's next?". Confidence: 0.7
