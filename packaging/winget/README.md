# WinGet release steps

The Windows release workflow builds a per-user Inno Setup installer and creates a ZIP containing versioned WinGet manifests with the installer's SHA-256 hash. A Windows build also runs for pull requests and pushes to `main`. On a `vMAJOR.MINOR.PATCH` tag, the installer and manifest ZIP are attached to a draft GitHub release.

Before publishing through the community WinGet source:

1. Make the FileSync GitHub repository public so WinGet validation can download the release installer without authentication.
2. Publish the generated draft release. Keep the installer URL version-specific; do not replace old release assets.
3. Extract `FileSync-<version>-winget-manifests.zip` and submit its manifest directory under `manifests/s/smizu-clang/FileSync/<version>/` in `microsoft/winget-pkgs` as a pull request.
4. Run `winget validate --manifest <manifest-directory>` before submitting, and confirm the pull request validation checks pass.

The package identifier is `smizu-clang.FileSync`. The x64 installer installs machine-wide and requires administrator elevation so it can install the official Microsoft Visual C++ runtime needed by the Qt build. The repository's current visibility must be changed to public before community submission; the release workflow does not change repository visibility or submit a WinGet pull request.
