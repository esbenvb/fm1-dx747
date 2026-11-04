# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# DX747: check a Windows machine before running sdk\Build.ps1. It only reads
# files and changes nothing. It repeats, one at a time, the checks Build.ps1
# makes before it compiles anything, and says which one fails and what to do.
# Lines marked FAIL would stop Build.ps1; lines marked advice are only things that
# commonly go wrong.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\dx747\windows\Check-Environment.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\dx747\windows\Check-Environment.ps1 -Toolchain D:\JL\pi32
#
# Written for Windows PowerShell 5.1 (the one that ships with Windows 10 and 11);
# PowerShell 7 works too. See docs\BUILD-WINDOWS.md.
param([string]$Toolchain = $env:FM1_PI32_TOOLCHAIN)
$ErrorActionPreference = 'Continue'
$script:failures = 0

function Ok([string]$text) { Write-Host ('  ok      ' + $text) -ForegroundColor Green }
function Bad([string]$text, [string]$hint) {
    $script:failures++
    Write-Host ('  FAIL    ' + $text) -ForegroundColor Red
    if ($hint) { Write-Host ('          ' + $hint) -ForegroundColor Yellow }
}
function Warn([string]$text, [string]$hint) {
    Write-Host ('  advice  ' + $text) -ForegroundColor Yellow
    if ($hint) { Write-Host ('          ' + $hint) -ForegroundColor Yellow }
}
function Note([string]$text) { Write-Host ('          ' + $text) }
function Head([string]$text) { Write-Host ''; Write-Host $text -ForegroundColor Cyan }
function Hash([string]$file) { (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }

Head '1. This machine'
# The language mode is checked first: in a locked-down mode even the calls below can fail.
if ($ExecutionContext.SessionState.LanguageMode -eq 'FullLanguage') {
    Ok 'PowerShell language mode is FullLanguage (Add-Type works)'
} else {
    Bad ('PowerShell language mode is ' + $ExecutionContext.SessionState.LanguageMode) 'The packaging step compiles sdk\Package.cs with Add-Type, which this mode blocks. It is usually a company policy (AppLocker or WDAC): use a personal machine or ask IT.'
    exit 1
}
Note ('PowerShell ' + $PSVersionTable.PSVersion + ' on ' + [Environment]::OSVersion.VersionString)
if ([Environment]::Is64BitOperatingSystem) { Ok '64-bit Windows' } else { Warn '32-bit Windows' 'The JieLi toolchain installer is for 64-bit Windows.' }
Note ('Execution policy: ' + (Get-ExecutionPolicy) + '  (start the build with -ExecutionPolicy Bypass, as the guide shows)')
foreach ($tool in @('curl.exe', 'tar.exe')) {
    if (Get-Command $tool -ErrorAction SilentlyContinue) { Ok ($tool + ' (needed only for build.bat --package, to fetch the WASI SDK)') }
    else { Warn ($tool + ' not found') 'Only build.bat --package needs it. It ships with Windows 10 (1803 and later) and 11; check that C:\Windows\System32 is on PATH.' }
}
$vcrt = Join-Path $env:WINDIR 'System32\msvcp140.dll'
if (Test-Path -LiteralPath $vcrt) {
    $vcrtInfo = (Get-Item -LiteralPath $vcrt).VersionInfo
    $vcrtLabel = $vcrtInfo.FileMajorPart.ToString() + '.' + $vcrtInfo.FileMinorPart + '.' + $vcrtInfo.FileBuildPart
    if (($vcrtInfo.FileMajorPart -gt 14) -or (($vcrtInfo.FileMajorPart -eq 14) -and ($vcrtInfo.FileMinorPart -ge 40))) {
        Ok ('Microsoft Visual C++ runtime ' + $vcrtLabel + ' (the WASI SDK compiler needs 14.40 or newer)')
    } else {
        Warn ('Microsoft Visual C++ runtime is ' + $vcrtLabel + ', older than 14.40') 'Only build.bat --package needs this: the WASI SDK compiler crashes with no message (exit code -1073741819, faulting module MSVCP140.dll) on an older runtime. Install the latest Microsoft Visual C++ Redistributable for x64 (https://aka.ms/vs/17/release/vc_redist.x64.exe), then run this check again.'
    }
} else {
    Warn 'MSVCP140.dll not found in System32' 'Only build.bat --package needs it. Install the latest Microsoft Visual C++ Redistributable for x64 (https://aka.ms/vs/17/release/vc_redist.x64.exe).'
}

Head '2. Where the source is'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
Note $root
if ($root.Length -gt 90) {
    Warn ('The path is long (' + $root.Length + ' characters)') 'Windows limits paths to 260 characters and the flashing page unpacks a compiler with files about 165 characters below this folder (build.bat --package). Clone the repository to a shorter path (any short path works, for example C:\fm1).'
} else { Ok 'path length is fine' }
if ($root -match '[^\x20-\x7E]') { Warn 'The path contains non-ASCII characters' 'Some of the tools may not cope. Clone the repository to a plain ASCII path (any works, for example C:\fm1).' }
if ($root -match 'OneDrive') { Warn 'The folder is inside OneDrive' 'OneDrive can lock or rewrite files while the build runs. Clone the repository to a folder outside OneDrive (any works, for example C:\fm1).' }
$sdk = Join-Path $root 'sdk'

Head '3. The files Build.ps1 checks before compiling'
foreach ($rel in @('sdk\Build.ps1', 'sdk\manifest.json', 'sdk\synth.json', 'sdk\Package.cs', 'app\version.h', 'app\synth\engine.h', 'sdk\lib\libfm1_engine.a')) {
    if (Test-Path -LiteralPath (Join-Path $root $rel)) { Ok $rel } else { Bad ('missing: ' + $rel) 'This folder is not a complete copy of the source tree. Copy it again.' }
}
$manifestPath = Join-Path $sdk 'manifest.json'
$synthPath = Join-Path $sdk 'synth.json'
if (-not ((Test-Path -LiteralPath $manifestPath) -and (Test-Path -LiteralPath $synthPath))) {
    Write-Host ''
    Write-Host 'Cannot go on without sdk\manifest.json and sdk\synth.json.' -ForegroundColor Red
    exit 1
}
$manifest = Get-Content -LiteralPath $manifestPath -Encoding UTF8 -Raw | ConvertFrom-Json
$synth = Get-Content -LiteralPath $synthPath -Encoding UTF8 -Raw | ConvertFrom-Json
if ($manifest.abi -eq 131072) { Ok 'sdk\manifest.json is for the 128-frame SDK' } else { Bad 'sdk\manifest.json has an unexpected abi' 'Build.ps1 stops with "This application requires the matching 128-frame SDK". Get the source again.' }

# 3a. the sdk payload, in the order Build.ps1 checks it (the audio contract comes first)
if (($synth.schema -eq 4) -and ($synth.api -eq 8) -and ($synth.kernel_abi -eq 3) -and ($synth.block_frames -eq 128) -and ($synth.sample_rate -eq 44100)) {
    Ok 'sdk\synth.json describes the expected engine (api 8, 128-frame blocks, 44.1 kHz)'
} else { Bad 'sdk\synth.json describes a different engine' 'Build.ps1 stops with "Native DSP kernels and SDK audio contracts do not match". Get the source again.' }
$blockHeader = Join-Path $sdk 'include\fm1_block.h'
if ((Test-Path -LiteralPath $blockHeader) -and ((Hash $blockHeader) -eq $synth.sdk_block_sha256)) { Ok 'sdk\include\fm1_block.h matches' }
else { Bad 'sdk\include\fm1_block.h does not match sdk\synth.json' 'Build.ps1 stops with "Native DSP kernels and SDK audio contracts do not match". The file was changed or converted to CRLF line endings: get the whole source again (a clone or a git archive, see the guide).' }
$engineLib = Join-Path $sdk 'lib\libfm1_engine.a'
if ((Test-Path -LiteralPath $engineLib) -and ((Hash $engineLib) -eq $synth.native_sha256)) { Ok 'sdk\lib\libfm1_engine.a is the expected engine library' }
else { Bad 'sdk\lib\libfm1_engine.a is not the expected engine library' 'Build.ps1 stops with "Native DSP kernels and SDK audio contracts do not match". The file is damaged or was converted as text: get it again.' }
$engineHeader = Join-Path $root 'app\synth\engine.h'
if ((Test-Path -LiteralPath $engineHeader) -and ([IO.File]::ReadAllText($engineHeader) -cmatch '(?m)^#define FM1_DSP_ABI 3U\s*$')) { Ok 'app\synth\engine.h declares DSP ABI 3' }
else { Bad 'app\synth\engine.h does not declare DSP ABI 3' 'Build.ps1 stops with "Unsupported native DSP ABI". Restore the file.' }
$pinned = @($synth.portable_contracts.PSObject.Properties)
$badInc = @()
foreach ($entry in $pinned) {
    $full = Join-Path $root ('app\synth\' + $entry.Name)
    if ((-not (Test-Path -LiteralPath $full)) -or ((Hash $full) -ne $entry.Value)) { $badInc += $entry.Name }
}
if ($pinned.Count -eq 0) { Ok 'this SDK pins no DSP coefficient tables, nothing to compare' }
elseif ($badInc.Count -eq 0) { Ok ('all ' + $pinned.Count.ToString() + ' pinned DSP coefficient tables match') }
else { Bad ('changed or missing: ' + ($badInc -join ', ')) 'Build.ps1 stops with "Fixed DSP coefficients differ". Usually line endings: these files must use LF, not CRLF (see the guide).' }

# 3b. every file the manifest lists, and no others
$changed = @()
$expected = @{}
$count = 0
foreach ($file in $manifest.files.PSObject.Properties) {
    $count++
    $expected[$file.Name] = $true
    $full = Join-Path $root $file.Name
    if (-not (Test-Path -LiteralPath $full)) { $changed += ($file.Name + ' (missing)') }
    elseif ((Hash $full) -ne $file.Value) { $changed += $file.Name }
}
if ($count -eq 0) { Bad 'sdk\manifest.json lists no files' 'It is damaged. Get the source again.' }
elseif ($changed.Count -eq 0) { Ok ('all ' + $count + ' SDK files match sdk\manifest.json') }
else {
    Bad ($changed.Count.ToString() + ' SDK file(s) differ from sdk\manifest.json') 'Build.ps1 stops with "SDK payload changed" for a changed file (a missing one ends with "Cannot find path"). Get the source again without editing anything under sdk\ (the guide explains the usual cause, line endings):'
    $changed | Select-Object -First 10 | ForEach-Object { Note ('  ' + $_) }
}
$extra = @()
foreach ($file in Get-ChildItem -LiteralPath $sdk -Recurse -Force -File) {
    $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
    if (($relative -ne 'sdk/manifest.json') -and (-not $expected.ContainsKey($relative))) { $extra += $relative }
}
if ($extra.Count -eq 0) { Ok 'no extra files inside sdk\' }
else {
    Bad ($extra.Count.ToString() + ' extra file(s) inside sdk\') 'Build.ps1 stops with "Unexpected SDK file". Delete these (usually Thumbs.db, desktop.ini, .DS_Store or an editor leftover):'
    $extra | Select-Object -First 10 | ForEach-Object { Note ('  ' + $_) }
}

# 3c. the version header
$versionHeader = Join-Path $root 'app\version.h'
if (Test-Path -LiteralPath $versionHeader) {
    $versionText = [IO.File]::ReadAllText($versionHeader)
    if (($versionText -cmatch '(?m)^#define FM1_RELEASE_VERSION "(V(?:0[1-9]|[1-9][0-9]{1,2}))"\s*$') -and ($versionText -cmatch '(?m)^#define FM1_UPDATE_PRODUCT "(FM-1_8[0-9]{6})"\s*$')) { Ok 'app\version.h has a valid release version and product' }
    else { Bad 'app\version.h has no valid FM1_RELEASE_VERSION / FM1_UPDATE_PRODUCT' 'Build.ps1 stops with "Set FM1_RELEASE_VERSION in app/version.h". Restore the file.' }
}

Head '4. The JieLi toolchain (it must be version 2.5.3)'
# Build.ps1 uses only the value it is given (-Toolchain, else FM1_PI32_TOOLCHAIN); this script also looks in the usual places.
function Find-Toolchain([string]$candidate) {
    if (-not $candidate) { return $null }
    if (-not (Test-Path -LiteralPath $candidate)) { return $null }
    $dir = $candidate
    if (Test-Path -LiteralPath (Join-Path $dir 'clang.exe')) { $dir = Split-Path -Parent $dir }   # the bin folder itself was given
    if (Test-Path -LiteralPath (Join-Path $dir 'bin\clang.exe')) { return $dir }
    return $null
}
$found = $null
if ($Toolchain) {
    $found = Find-Toolchain $Toolchain
    if (-not $found) {
        Bad ('-Toolchain / FM1_PI32_TOOLCHAIN does not contain bin\clang.exe: ' + $Toolchain) 'Build.ps1 uses only this value and would stop with "Install JieLi Windows toolchain 2.5.3". Point it at the pi32 folder, or clear it with: Remove-Item Env:FM1_PI32_TOOLCHAIN'
    }
}
if (-not $found) {
    foreach ($candidate in @('C:\JL\pi32', 'D:\JL\pi32', (Join-Path $env:ProgramFiles 'JL\pi32'))) {
        $found = Find-Toolchain $candidate
        if ($found) { Note ('found a toolchain at ' + $found + ' (pass it as -Toolchain)'); break }
    }
}
if (-not $found) {
    Bad 'No pi32\bin\clang.exe found' 'Install JieLi toolchain 2.5.3 (the guide has the link), then run this again. If it is installed somewhere unusual, pass its pi32 folder: -Toolchain D:\path\to\pi32'
    $roots = @('C:\JL', 'D:\JL', $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA, $env:USERPROFILE) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    $hits = @()
    if (@($roots).Count -gt 0) { $hits = @(Get-ChildItem -LiteralPath $roots -Recurse -Depth 4 -Filter clang.exe -ErrorAction SilentlyContinue | Select-Object -First 5) }
    foreach ($hit in $hits) { Note ('found a clang.exe at ' + $hit.FullName + ' (the folder above its bin folder is what -Toolchain needs)') }
} else {
    Note ('using ' + $found)
    $bad = @()
    $n = 0
    foreach ($entry in $manifest.external_toolchain.files.PSObject.Properties) {
        $n++
        $file = Join-Path $found $entry.Name
        if (-not (Test-Path -LiteralPath $file)) { $bad += ($entry.Name + ' (missing)') }
        elseif ((Hash $file) -ne $entry.Value) { $bad += ($entry.Name + ' (different)') }
    }
    if ($n -eq 0) { Bad 'sdk\manifest.json pins no toolchain files' 'It is damaged. Get the source again.' }
    elseif ($bad.Count -eq 0) { Ok ('all ' + $n + ' toolchain files that the SDK pins by hash match') }
    else {
        Bad ($bad.Count.ToString() + ' of ' + $n + ' pinned toolchain files do not match') 'Build.ps1 stops with "External toolchain does not match the SDK". This SDK needs exactly JieLi 2.5.3; older kits pinned 2.5.2 and reject 2.5.3. Uninstall any other version, install 2.5.3 and run this again. First differences:'
        $bad | Select-Object -First 8 | ForEach-Object { Note ('  ' + $_) }
        $clang = Join-Path $found 'bin\clang.exe'
        if (Test-Path -LiteralPath $clang) {
            Note ('clang.exe here:     ' + (Hash $clang))
            Note ('clang.exe expected: ' + $manifest.external_toolchain.files.'bin/clang.exe')
        }
    }
}

Write-Host ''
if ($script:failures -eq 0) {
    Write-Host 'All checks passed. Build with:' -ForegroundColor Green
    Write-Host ('  cd "' + $root + '"')
    Write-Host ('  powershell -NoProfile -ExecutionPolicy Bypass -File sdk\Build.ps1 -FirmwareOnly -Toolchain "' + $found + '"')
} else {
    Write-Host ($script:failures.ToString() + ' check(s) failed. Fix the FAIL lines above, the top one first, and run this again.') -ForegroundColor Red
    exit 1
}
