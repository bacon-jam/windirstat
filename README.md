# WinDirStat - Windows Directory Statistics

## Description

WinDirStat is a disk usage analyzer and cleanup assistant for Microsoft Windows. Scan drives, folders, network shares, or connected MTP devices, then explore disk usage through sortable file lists, file-type statistics, and interactive treemaps, sunbursts, or flame graphs.

Find large or duplicate files, inspect permissions, estimate storage costs, export reports, and run cleanup or deleted-file recovery tools. See the [website](https://windirstat.net/) for an overview and the [user guide](https://github.com/windirstat/windirstat/wiki) for usage details.

### Major features

* Scan multiple drives or folders together, with pause/resume, refresh, and accelerated local NTFS scanning when elevated
* Explore largest files, file types, logical versus physical size, hardlinks, and free/unknown space with configurable visualizations
* Search scanned results by name, size, file/folder type, or owner; filter scans by path, name, size, or age, with regular-expression support
* Detect duplicates using configurable hashes and cloud-file safeguards; optional sparse hashing labels sampled matches as **Probable duplicates**
* Inspect file and folder permission entries and estimate storage tier costs by file modification age
* Watch file-system changes, save/load scans as CSV or JSON, and export scan, duplicate, or permission reports from the command line
* [Recover deleted files](https://github.com/windirstat/windirstat/wiki/Recovering-Files) from local NTFS, exFAT, and FAT drives with administrator access; recovery depends on surviving metadata and file data
* Open, move, or delete files; run Windows cleanup tools, NTFS compression, hardlink deduplication, and custom cleanup commands
* Customize layouts, columns, colors, fonts, and toolbar size, with dark mode, portable settings, translations, and Explorer integration

For changes in recent versions, please check out [the change log](CHANGELOG.md).

### Getting started

1. Choose drives or **Individual Folders** in **Select Drives**. Use the add-folder button to scan several folders together. Connected MTP devices appear under **Individual Drives**.
2. Enable **Scan for duplicate files** only when needed; hashing adds file reads. For accelerated full-drive NTFS scans, use **File > Run Elevated**.
3. Sort **All Files** or open **Largest Files**, select an item to locate it in the visualization, and use `Ctrl+F` to search. Right-click column headers to choose details and use **View** to switch graphs.
4. Review items in Explorer before cleanup, then refresh affected results. Use **File > Save Results To CSV/JSON** to keep a scan snapshot.

For scripting, use `/SaveTo`, `/SaveDupesTo`, or `/SavePermsTo` followed by an output filename and scan targets. Save modes write a report and exit; `/LoadFrom` reopens a saved scan. See [command-line examples and export formats](https://github.com/windirstat/windirstat/wiki/Command-Line-and-CSV).

### Installation

The recommended way to install WinDirStat is with a package manager, which also makes future updates easier:

* Install from the [Microsoft Store](https://apps.microsoft.com/detail/9ph1gl95p3wf) for Store-managed installation and updates
* Install with `winget install -e --id WinDirStat.WinDirStat` (or use `winget upgrade` later)
* Install with `choco install windirstat` (or use `choco upgrade windirstat` later)
* Install with `scoop install extras/windirstat` (requires `scoop bucket add extras`)

If you prefer a manual installer, need a portable archive, or want to browse older versions and beta builds, use the [GitHub releases page](https://github.com/windirstat/windirstat/releases/). If you are not sure which file to choose, download the **64-bit MSI installer**.

| Download | Best for | What is inside |
| --- | --- | --- |
| [Microsoft Store app](https://apps.microsoft.com/detail/9ph1gl95p3wf) | Users who want one-click installation and Store-managed updates | Installs WinDirStat through the Microsoft Store app experience on supported Windows systems. |
| [WinDirStat-x64.msi](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat-x64.msi) | Most users on modern Intel or AMD 64-bit Windows PCs | Standard Windows installer for 64-bit systems. Adds WinDirStat to the Start menu and installs it like a normal app. |
| [WinDirStat-arm64.msi](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat-arm64.msi) | Windows on ARM devices, including newer Surface devices and other Snapdragon-based laptops | Standard Windows installer built for ARM64 Windows. |
| [WinDirStat-x86.msi](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat-x86.msi) | Older 32-bit Windows installations | Standard Windows installer for 32-bit systems. |
| [MSIX bundle](https://github.com/windirstat/windirstat/releases/latest) | Windows App Installer or Store-style deployment across different CPU types | If the release includes an `.msixbundle` asset, it can contain packages for multiple CPU types and Windows chooses the right package for your device. |
| [WinDirStat.zip](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat.zip) | Portable use, testing, or running without an installer | Zip archive containing the WinDirStat executables. Extract it first, then run the executable for your CPU type. |
| [WinDirStat.7z](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat.7z) | Portable use when you already have 7-Zip installed | Same kind of portable executable archive as the zip file, usually with a smaller download size. |

## Copyright / Licenses

* Copyright © WinDirStat Team ([windirstat.net](https://windirstat.net/))

The application itself is distributed under the terms of the [GPL v3 or later](windirstat/res/license.txt), but parts of the source code are also available under more lenient license terms.

The logo and all derivatives are available under the terms of the Creative
Commons license [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).

## Compatibility

WinDirStat 2.x has been developed for and tested on the following operating systems. It may work on older or newer operating systems, but those systems are not supported.

* Windows 7
* Windows 8
* Windows 8.1
* Windows 10
* Windows 11
* Windows Server 2008 R2
* Windows Server 2012
* Windows Server 2012 R2
* Windows Server 2016
* Windows Server 2019
* Windows Server 2022
* Windows Server 2025

## Resources

* A [website](https://windirstat.net/)
* The [user guide and reference](https://github.com/windirstat/windirstat/wiki)
* A [blog](https://blog.windirstat.net/)
* Twitter/X as [@windirstat](https://x.com/windirstat)
* SubReddit [r/WinDirStat](https://www.reddit.com/r/WinDirStat/)

Find a more up-to-date list of resources on the website and the blog at any point in time.

## Official Downloads and Malware Warning

WinDirStat's popularity has led to unofficial websites that copy the project's name, branding, or downloads. These sites are not operated by the WinDirStat team, may offer outdated or modified files, and may expose users to malware.

For your safety, install WinDirStat only through the Microsoft Store link and package managers listed above, the official [GitHub releases](https://github.com/windirstat/windirstat/releases/), or links from [windirstat.net](https://windirstat.net/). The team reports impersonation sites when possible, but takedowns are not always successful.

## Building

WinDirStat can be built with Visual Studio 2022 or later. A Visual Studio solution file can be loaded from `windirstat.sln`.

### Indexing benchmarks

Build the application once to obtain the local xxHash dependency, then compare an explicit Git baseline with a snapshot of the current working tree:

```powershell
pwsh -File tests/Benchmark-Indexing.ps1 -BaselineRef 324dc5b -PlatformToolset v143
```

Both versions use the same compiler, Release x64 configuration, dependency files and scanner harness. The default run uses a 1 GiB synthetic MFT, 12,000 real files, two warmup pairs and 12 measured pairs, including duplicate detection. Keep the machine idle; use `-OutputFolder` with a **new directory outside synchronized storage** for steadier results. Allow about 8 GiB plus build artifacts. Builds and fixtures are retained for inspection.

The scanner lane times the production `LoadRoot` reader, parser and merge code with real unbuffered, overlapped file reads. It verifies every filename, parent, reference, size, timestamp and hardlink after each sample. Cases cover contiguous and separated extents, skewed layouts with 87.5% of the records in the first extent, a small fragmented MFT, a single-worker control, and simulated seek-penalty/unknown-device policies. Samples also record read-worker invocations, so a concurrency change is visible. Simulating that policy does not simulate physical HDD latency. The application lane times launch through folder scan, tree construction, CSV export and exit. Every report is checked against the generated tree and compared across versions, including all exported fields. This lane uses ordinary folder traversal, not the raw-volume fast path.

Trials alternate baseline/candidate order, shuffle cases using a recorded seed, exclude warmups, and retain all measured samples. `summary.txt` and `summary.json` report medians, median absolute deviations, CPU time, peak memory and a paired bootstrap 95% interval for geometric mean time reduction. Application memory is observed while the process is alive and may miss its final peak; unavailable samples remain null. Scanner memory is the process high-water mark read immediately after `LoadRoot`, before verification. Positive reduction means faster. A result is classified as faster/slower only when the whole interval exceeds the default 3% practical threshold; otherwise it is equivalent within that margin or inconclusive. These are per-case exploratory intervals, not a guarantee across machines or a correction for multiple comparisons.

The duplicate lane uses full-file xxHash by default (`-HashAlgorithm 5`); select `-HashAlgorithm 2` for SHA-256. It compares identical 8,193-byte files in one flat folder and across 64 folders, and includes unique sizes (no hashing), varied sizes, identical small files, distinct files of one size, and 2 MiB files with matching prefixes but different later content. Identical and early-distinct files at 16, 32, and 64 KiB measure the benefit and extra-read cost of small-file hashing cutoffs. Expected groups come from independently computed whole-file SHA-256 hashes. Every result must contain exactly the expected members without splitting or merging content groups; SHA-256 runs also check digest prefixes directly. Every exported field is compared across versions. Input creation and verification are outside timing. Small duplicate corpora are capped at 16,384 files each, the cutoff corpora at 8,192, and the 2 MiB corpus at 128 files; default inputs occupy about 2.3 GiB. Sampled hashing is covered by correctness tests, not this lane.

Use `-Lane Scanner`, `-Lane Application`, or `-Lane Duplicates` to isolate a timing scope. The duplicate lane builds both full applications without creating synthetic MFT files:

```powershell
pwsh -File tests/Benchmark-Indexing.ps1 -BaselineRef 324dc5b -PlatformToolset v143 -Lane Duplicates `
    -Threads 8 -RequireImprovement duplicates-flat -FailOnRegression
```

Use `-Threads 16` for a separate worker-count comparison; both builds always receive the same value. More workers can increase I/O contention, so compare all cases.

`-RecordCount 1024 -FileCount 100 -Pairs 6 -Warmups 1` is a tooling smoke test, not representative performance evidence. Exit code 1 means execution/correctness failed. `-FailOnRegression` and `-RequireImprovement scanner-contiguous` add performance gates (exit code 2); an inconclusive result cannot satisfy an improvement gate. Without a gate, regressions are reported without making the completed experiment fail.

```powershell
pwsh -File tests/Test-Benchmark.ps1
pwsh -File tests/Test-Scanner.ps1 -PlatformToolset v143 -RecordCount 1048576
```

These are repeated warm measurements, not cold initial-drive scans or GUI readiness timings. Whole-volume speed still needs an elevated, controlled NTFS-volume experiment with equivalent contents, settings and cache conditions. Do not compare the scanner's `LoadRoot` time directly with Everything's filename-only indexing or claim a whole-drive speedup from the fixture results alone.

## Contributors

You can contribute by responding to issues, [developing](https://github.com/windirstat/windirstat/wiki/Developers) source code, or developing [translations](https://github.com/windirstat/windirstat/wiki/Contribute-Translation).

Thank you to everyone who has helped shape WinDirStat over the years.

<a href="https://github.com/windirstat/windirstat/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=windirstat/windirstat&max=1000" alt="WinDirStat contributor tiles" />
</a>

For additional historical contributors, testers, and translators, please check out [the contributors page](CONTRIBUTORS.md).

## Logo

![WinDirStat logo](windirstat/logos/logo_256px.png)
