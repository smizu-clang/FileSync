param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$InstallerUrl,
    [Parameter(Mandatory = $true)][string]$InstallerPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$identifier = 'smizu-clang.FileSync'
$manifestVersion = '1.28.0'
$hash = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$versionManifest = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.$manifestVersion.schema.json
PackageIdentifier: $identifier
PackageVersion: $Version
DefaultLocale: ja-JP
ManifestType: version
ManifestVersion: $manifestVersion
"@

$japaneseLocale = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.$manifestVersion.schema.json
PackageIdentifier: $identifier
PackageVersion: $Version
PackageLocale: ja-JP
Publisher: smizu-clang
PublisherUrl: https://github.com/smizu-clang
PublisherSupportUrl: https://github.com/smizu-clang/FileSync/issues
PackageName: FileSync
PackageUrl: https://github.com/smizu-clang/FileSync
License: Apache-2.0
LicenseUrl: https://github.com/smizu-clang/FileSync/blob/main/LICENSE
ShortDescription: ローカルフォルダーを同期するデスクトップアプリ
Description: |
  FileSync はローカルフォルダーの片方向・双方向同期、重複ファイルの検出と削除を行う Qt デスクトップアプリです。
Tags:
  - file-sync
  - synchronization
  - backup
  - qt
ManifestType: defaultLocale
ManifestVersion: $manifestVersion
"@

$englishLocale = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.locale.$manifestVersion.schema.json
PackageIdentifier: $identifier
PackageVersion: $Version
PackageLocale: en-US
Publisher: smizu-clang
PublisherUrl: https://github.com/smizu-clang
PublisherSupportUrl: https://github.com/smizu-clang/FileSync/issues
PackageName: FileSync
PackageUrl: https://github.com/smizu-clang/FileSync
License: Apache-2.0
LicenseUrl: https://github.com/smizu-clang/FileSync/blob/main/LICENSE
ShortDescription: Desktop application for synchronizing local folders
Description: |
  FileSync is a Qt desktop application for one-way and bidirectional folder sync, with duplicate file detection and removal.
Tags:
  - file-sync
  - synchronization
  - backup
  - qt
ManifestType: locale
ManifestVersion: $manifestVersion
"@

$installerManifest = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.$manifestVersion.schema.json
PackageIdentifier: $identifier
PackageVersion: $Version
InstallerType: inno
Installers:
  - Architecture: x64
    Scope: machine
    ElevationRequirement: elevationRequired
    InstallerUrl: $InstallerUrl
    InstallerSha256: $hash
ManifestType: installer
ManifestVersion: $manifestVersion
"@

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory "$identifier.yaml"), $versionManifest, $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory "$identifier.locale.ja-JP.yaml"), $japaneseLocale, $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory "$identifier.locale.en-US.yaml"), $englishLocale, $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory "$identifier.installer.yaml"), $installerManifest, $utf8NoBom)
