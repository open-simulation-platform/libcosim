#!/usr/bin/env bash
# Bash runs this block; PowerShell sees it as a block comment.
echo "\" >$null <#" >/dev/null
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
build="$root/build/Release"
fix=false
jobs=
format=false
tidy=false
paths=()
while (($#)); do
    case "$1" in
        -h|--help)
            printf '%s\n' \
                'Usage: bash clang-check.ps1 [options] [--] [path ...]' \
                '' \
                'Check C/C++ files recursively without modifying them; defaults to src and include.' \
                'Relative paths are resolved from the current directory.' \
                'Clang-tidy checks selected sources and also checks selected headers directly.' \
                '' \
                'Options:' \
                '  -h, --help                         Show this help and exit.' \
                '  --fix                              Apply fixes with the selected tools.' \
                '  --format                           Run clang-format only (unless --tidy is also set).' \
                '  --tidy                             Run clang-tidy only (unless --format is also set).' \
                '  -BuildDirectory, --build-dir DIR   Compilation database directory (default: build/Release).' \
                '  -Jobs, --jobs N                    Read-only source workers (default: up to 4).' \
                '  --                                 Treat remaining arguments as paths.'
            exit 0 ;;
        --fix)
            fix=true
            shift ;;
        --format)
            format=true
            shift ;;
        --tidy)
            tidy=true
            shift ;;
        -BuildDirectory|--build-dir)
            if (($# < 2)); then echo "Missing compilation database directory." >&2; exit 1; fi
            build=$2
            shift 2 ;;
        -Jobs|--jobs)
            if (($# < 2)) || [[ ! $2 =~ ^[1-9][0-9]{0,9}$ ]] || ((10#$2 > 2147483647)); then
                echo 'Jobs must be a positive integer no greater than 2147483647.' >&2
                exit 1
            fi
            jobs=$2
            shift 2 ;;
        --)
            shift
            paths+=("$@")
            break ;;
        *)
            paths+=("$1")
            shift ;;
    esac
done
if ! $format && ! $tidy; then format=true; tidy=true; fi
if $tidy && [[ -z $jobs ]]; then
    jobs=1
    if cpus="$(getconf _NPROCESSORS_ONLN 2>/dev/null)" && [[ $cpus =~ ^[1-9][0-9]*$ ]]; then
        if ((cpus < 4)); then jobs=$cpus; else jobs=4; fi
    else
        echo 'Could not determine the CPU count; using one clang-tidy worker.' >&2
    fi
fi
if ((${#paths[@]} == 0)); then paths=("$root/src" "$root/include"); fi
if $tidy && [[ ! -f "$build/compile_commands.json" ]]; then
    echo "No compile_commands.json in $build" >&2
    exit 1
fi

for i in "${!paths[@]}"; do
    if [[ ${paths[i]} != /* ]]; then paths[i]="./${paths[i]}"; fi
    if [[ ! -e ${paths[i]} ]]; then echo "Path does not exist: ${paths[i]}" >&2; exit 1; fi
done
extensions=(
    -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx'
    -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx'
)
first_file="$(find "${paths[@]}" -type f \( "${extensions[@]}" \) -print | head -n 1)"
if [[ -z $first_file ]]; then echo 'No C/C++ sources or headers found.' >&2; exit 1; fi

tidy_patterns=()
headers=()
has_sources=false
if $tidy; then
    first_source="$(find "${paths[@]}" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \) -print | head -n 1)"
    if [[ -n $first_source ]]; then has_sources=true; fi
    while IFS= read -r -d '' header; do
        absolute="$(cd "$(dirname "$header")" && printf '%s/%s' "$(pwd -P)" "$(basename "$header")")" || exit 1
        duplicate=false
        for selected_header in "${headers[@]}"; do
            if [[ $selected_header == "$absolute" ]]; then duplicate=true; break; fi
        done
        if ! $duplicate; then headers+=("$absolute"); fi
    done < <(find "${paths[@]}" -type f \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) -print0)
    for path in "${paths[@]}"; do
        if [[ -d $path ]]; then
            absolute="$(cd "$path" && pwd -P)" || exit 1
        else
            absolute="$(cd "$(dirname "$path")" && printf '%s/%s' "$(pwd -P)" "$(basename "$path")")" || exit 1
        fi
        escaped="$(printf '%s' "$absolute" | sed 's/[][\\.^$*+?(){}|]/\\&/g')"
        if [[ $absolute == / ]]; then
            tidy_patterns+=('^/')
        elif [[ -d $path ]]; then
            tidy_patterns+=("^${escaped}(/|$)")
        else
            tidy_patterns+=("^${escaped}$")
        fi
    done
fi

tidy_check() {
    local header_filter workers=$jobs
    if (($#)); then
        workers=1
        echo 'Running clang-tidy (fix).'
    else
        echo 'Running clang-tidy (check).'
    fi
    header_filter="$(IFS='|'; printf '%s' "${tidy_patterns[*]}")"
    run-clang-tidy -p "$build" -j "$workers" -quiet "-header-filter=$header_filter" "$@" "${tidy_patterns[@]}"
}

tidy_headers() {
    local header result=0
    if (($#)); then
        echo 'Running clang-tidy on headers (fix).'
    else
        echo 'Running clang-tidy on headers (check).'
    fi
    for header in "${headers[@]}"; do
        echo "Checking header: $header"
        clang-tidy -p "$build" -quiet "$@" "$header" || result=1
    done
    return "$result"
}

summary_dir=
cleanup_summary() {
    if [[ -n $summary_dir ]]; then
        rm -f -- "$summary_dir/stdout" "$summary_dir/stderr"
        rmdir -- "$summary_dir"
    fi
}
trap cleanup_summary EXIT

summarize_stream() {
    awk -v counts="$1" -v root="$root" -v cwd="$(pwd -P)" -v platform="$OSTYPE" '
        function normalize(path, parts, n, i, count, result, drive) {
            if (platform ~ /^(msys|cygwin)/) {
                gsub(/\\/, "/", path)
                if (path ~ /^[A-Za-z]:\//) {
                    drive = tolower(substr(path, 1, 1))
                    path = (root ~ /^\/cygdrive\// ? "/cygdrive/" : "/") drive substr(path, 3)
                }
                path = tolower(path)
            }
            if (substr(path, 1, 1) != "/") path = cwd "/" path
            n = split(path, parts, "/")
            count = 0
            for (i = 1; i <= n; i++) {
                if (parts[i] == "" || parts[i] == ".") continue
                if (parts[i] == "..") {
                    if (count > 0) count--
                } else {
                    stack[++count] = parts[i]
                }
            }
            result = ""
            for (i = 1; i <= count; i++) result = result "/" stack[i]
            return result == "" ? "/" : result
        }
        BEGIN {
            if (platform ~ /^(msys|cygwin)/) {
                root = tolower(root)
                cwd = tolower(cwd)
            }
        }
        {
            line = $0
            gsub(sprintf("%c\\[[0-9;]*m", 27), "", line)
            external = 0
            if (line ~ /^((.+:[0-9]+(:[0-9]+)?:[[:space:]]*)|(clang(-tidy|-format)?(\.exe)?:[[:space:]]*)?)(fatal error|error|warning|note):/) {
                if (line ~ /(^|:[[:space:]]*)(fatal error|error):/) errors++
                else if (line ~ /(^|:[[:space:]]*)warning:/) warnings++
                else notes++
                if (line ~ /^.+:[0-9]+(:[0-9]+)?:[[:space:]]*(fatal error|error|warning):/) {
                    path = line
                    sub(/:[0-9]+(:[0-9]+)?:[[:space:]]*(fatal error|error|warning):.*/, "", path)
                    if (path !~ /^<.*>$/) {
                        path = normalize(path)
                        if (root != "/" && path != root && index(path, root "/") != 1) {
                            external = 1
                            externals++
                        }
                    }
                }
            }
            print $0 (external ? " [external]" : "")
            fflush()
        }
        END { printf "%d %d %d %d\n", errors, warnings, notes, externals > counts }
    '
}

summaries=()
run_with_summary() {
    local label=$1 result out_pid err_pid
    local out_errors out_warnings out_notes out_external err_errors err_warnings err_notes err_external
    shift
    summary_dir="$(mktemp -d "${TMPDIR:-/tmp}/clang-check.XXXXXXXX")" || return 1
    exec 3> >(summarize_stream "$summary_dir/stdout")
    out_pid=$!
    exec 4> >(summarize_stream "$summary_dir/stderr" >&2 3>&-)
    err_pid=$!
    "$@" >&3 2>&4
    result=$?
    exec 3>&- 4>&-
    wait "$out_pid" || result=1
    wait "$err_pid" || result=1
    if ! read -r out_errors out_warnings out_notes out_external < "$summary_dir/stdout" ||
       ! read -r err_errors err_warnings err_notes err_external < "$summary_dir/stderr"; then
        echo 'Could not collect diagnostic counts.' >&2
        cleanup_summary
        summary_dir=
        return 1
    fi
    cleanup_summary
    summary_dir=
    local suffix=
    local external=$((out_external + err_external))
    if ((external)); then suffix=", $external external"; fi
    if ((result != 0)); then suffix+=' (failed)'; fi
    summaries+=("$label: $((out_errors + err_errors)) errors, $((out_warnings + err_warnings)) warnings, $((out_notes + err_notes)) notes$suffix")
    return "$result"
}

format_check() {
    local mode=$1
    if [[ $mode == fix ]]; then
        echo 'Running clang-format (fix).'
        find "${paths[@]}" -type f \( "${extensions[@]}" \) \
            -exec clang-format --style=file -i {} +
    else
        echo 'Running clang-format (check).'
        find "${paths[@]}" -type f \( "${extensions[@]}" \) \
            -exec clang-format --style=file --dry-run --Werror {} +
    fi
}

status=0
if $fix; then
    if $tidy; then
        if $has_sources; then run_with_summary 'clang-tidy (fix)' tidy_check -fix || status=1; fi
        if ((${#headers[@]})); then run_with_summary 'clang-tidy headers (fix)' tidy_headers -fix || status=1; fi
    fi
    if $format; then
        run_with_summary 'clang-format (fix)' format_check fix || status=1
    fi
    if $tidy; then
        if $has_sources; then run_with_summary 'clang-tidy (check)' tidy_check || status=1; fi
        if ((${#headers[@]})); then run_with_summary 'clang-tidy headers (check)' tidy_headers || status=1; fi
    fi
else
    if $format; then
        run_with_summary 'clang-format (check)' format_check check || status=1
    fi
    if $tidy; then
        if $has_sources; then run_with_summary 'clang-tidy (check)' tidy_check || status=1; fi
        if ((${#headers[@]})); then run_with_summary 'clang-tidy headers (check)' tidy_headers || status=1; fi
    fi
fi
printf '\n%s\n' 'Clang findings summary (emitted diagnostics):'
printf '  %s\n' "${summaries[@]}"
exit "$status"
#>

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$build = Join-Path $PSScriptRoot 'build' 'Release'
$fix = $false
$jobs = [Math]::Min(4, [Environment]::ProcessorCount)
$format = $false
$tidy = $false
$paths = @()
for ($i = 0; $i -lt $args.Count; ++$i) {
    if ($args[$i] -in '-h', '--help') {
        @'
Usage: pwsh .\clang-check.ps1 [options] [--] [path ...]

Check C/C++ files recursively without modifying them; defaults to src and include.
Relative paths are resolved from the current directory.
Clang-tidy checks selected sources and also checks selected headers directly.

Options:
  -h, --help                         Show this help and exit.
  --fix                              Apply fixes with the selected tools.
  --format                           Run clang-format only (unless --tidy is also set).
  --tidy                             Run clang-tidy only (unless --format is also set).
  -BuildDirectory, --build-dir DIR   Compilation database directory (default: build\Release).
  -Jobs, --jobs N                    Read-only source workers (default: up to 4).
  --                                 Treat remaining arguments as paths.
'@
        exit 0
    } elseif ($args[$i] -ceq '--fix') {
        $fix = $true
    } elseif ($args[$i] -eq '--format') {
        $format = $true
    } elseif ($args[$i] -eq '--tidy') {
        $tidy = $true
    } elseif ($args[$i] -in '-BuildDirectory', '--build-dir') {
        if (++$i -ge $args.Count) { throw 'Missing compilation database directory.' }
        $build = $args[$i]
    } elseif ($args[$i] -in '-Jobs', '--jobs') {
        $requestedJobs = 0
        if (++$i -ge $args.Count -or $args[$i] -notmatch '^[1-9][0-9]*$' -or
            ![int]::TryParse([string]$args[$i], [ref]$requestedJobs)) {
            throw 'Jobs must be a positive integer no greater than 2147483647.'
        }
        $jobs = $requestedJobs
    } elseif ($args[$i] -eq '--') {
        if ($i + 1 -lt $args.Count) {
            $paths += $args[($i + 1)..($args.Count - 1)]
        }
        break
    } else {
        $paths += $args[$i]
    }
}
if (!$format -and !$tidy) { $format = $true; $tidy = $true }
if (!$paths.Count) { $paths = @((Join-Path $PSScriptRoot 'src'), (Join-Path $PSScriptRoot 'include')) }
if ($tidy) {
    if (!(Test-Path -LiteralPath (Join-Path $build 'compile_commands.json') -PathType Leaf)) {
        throw "No compile_commands.json in $build"
    }
    $build = (Resolve-Path -LiteralPath $build).ProviderPath
}

$files = @(Get-ChildItem -LiteralPath $paths -Recurse -File -Force |
    Where-Object { $_.Extension -cin '.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx' } |
    ForEach-Object { $_.FullName })
if (!$files.Count) { throw 'No C/C++ sources or headers found.' }

$tidyPatterns = @()
$headers = @()
$hasSources = $false
if ($tidy) {
    $hasSources = @($files | Where-Object { [IO.Path]::GetExtension($_) -cin '.c', '.cc', '.cpp', '.cxx' }).Count -gt 0
    $headers = @($files | Where-Object { [IO.Path]::GetExtension($_) -cin '.h', '.hh', '.hpp', '.hxx' } | Sort-Object -Unique)
    foreach ($path in $paths) {
        $resolved = (Resolve-Path -LiteralPath $path).ProviderPath
        $escaped = [regex]::Escape($resolved)
        if ($resolved -eq [IO.Path]::GetPathRoot($resolved)) {
            $tidyPatterns += "^$escaped"
        } elseif (Test-Path -LiteralPath $resolved -PathType Container) {
            $tidyPatterns += "^$escaped([\\/]|`$)"
        } else {
            $tidyPatterns += "^$escaped`$"
        }
    }
}

$status = 0
$summaries = [System.Collections.Generic.List[object]]::new()
$rootPath = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$pathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
function Invoke-WithSummary {
    param([string]$Label, [scriptblock]$Command)
    $summary = [PSCustomObject]@{ Label = $Label; Errors = 0; Warnings = 0; Notes = 0; External = 0; Failed = $false }
    & $Command 2>&1 | ForEach-Object {
        $line = [string]$_
        $plainLine = [regex]::Replace($line, '\x1b\[[0-9;]*m', '')
        if ($plainLine -match '^((.+:\d+(:\d+)?:\s*)|(clang(-tidy|-format)?(\.exe)?:\s*)?)(fatal error|error|warning|note):') {
            switch ($Matches[7]) {
                { $_ -in 'fatal error', 'error' } { $summary.Errors++ }
                'warning' { $summary.Warnings++ }
                'note' { $summary.Notes++ }
            }
        }
        if ($plainLine -match '^(?<path>.+?):\d+(?::\d+)?:\s*(?:fatal error|error|warning):' -and
            $Matches.path -notmatch '^<.*>$') {
            $diagnosticPath = [IO.Path]::GetFullPath($Matches.path, (Get-Location).ProviderPath)
            if (!$diagnosticPath.Equals($rootPath, $pathComparison) -and
                !$diagnosticPath.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, $pathComparison)) {
                $summary.External++
                $line += ' [external]'
            }
        }
        if ($_ -is [System.Management.Automation.ErrorRecord]) {
            [Console]::Error.WriteLine($line)
        } else {
            Write-Output $line
        }
    }
    $summary.Failed = $script:phaseFailed
    $summaries.Add($summary)
}

function Set-PhaseFailed {
    $script:phaseFailed = $true
    $script:status = 1
}

function Invoke-Format {
    param([string[]]$Files, [bool]$Fix)
    if ($Fix) { Write-Host 'Running clang-format (fix).' } else { Write-Host 'Running clang-format (check).' }
    $formatArgs = if ($Fix) { @('--style=file', '-i') } else { @('--style=file', '--dry-run', '--Werror') }
    $batch = [System.Collections.Generic.List[string]]::new()
    $length = 0
    foreach ($file in $Files) {
        $cost = 2 * $file.Length + 4
        # Leave room for cmd.exe wrappers and argument quoting.
        if ($batch.Count -and $length + $cost -gt 7000) {
            & clang-format @formatArgs @batch
            if ($LASTEXITCODE -ne 0) { Set-PhaseFailed }
            $batch.Clear()
            $length = 0
        }
        $batch.Add($file)
        $length += $cost
    }
    if ($batch.Count) {
        & clang-format @formatArgs @batch
        if ($LASTEXITCODE -ne 0) { Set-PhaseFailed }
    }
}

function Invoke-TidyCheck {
    param([bool]$Fix = $false)
    if ($Fix) { Write-Host 'Running clang-tidy (fix).' } else { Write-Host 'Running clang-tidy (check).' }
    $workerCount = if ($Fix) { 1 } else { $jobs }
    $tidyArgs = @('-p', $build, '-j', $workerCount, '-quiet', "-header-filter=$($tidyPatterns -join '|')")
    if ($Fix) { $tidyArgs += '-fix' }
    $runner = Get-Command run-clang-tidy -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$runner) {
        $runner = Get-Command run-clang-tidy.py -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (!$runner) {
        Write-Error 'run-clang-tidy is required on PATH.' -ErrorAction Continue
        Set-PhaseFailed
        return
    }
    $extension = if ($runner.CommandType -eq 'Application') { [IO.Path]::GetExtension($runner.Source) } else { $null }
    if ($IsWindows -and $runner.CommandType -eq 'Application' -and $extension -in '', '.py') {
        $python = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        $pythonArgs = @()
        if (!$python) {
            $python = Get-Command py -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            $pythonArgs = @('-3')
        }
        if (!$python) {
            Write-Error 'Python 3 is required on PATH to run the run-clang-tidy script.' -ErrorAction Continue
            Set-PhaseFailed
            return
        }
        & $python.Source @pythonArgs $runner.Source @tidyArgs @tidyPatterns
    } else {
        & $runner @tidyArgs @tidyPatterns
    }
    if ($LASTEXITCODE -ne 0) { Set-PhaseFailed }
}

function Invoke-TidyHeaders {
    param([bool]$Fix = $false)
    if ($Fix) { Write-Host 'Running clang-tidy on headers (fix).' } else { Write-Host 'Running clang-tidy on headers (check).' }
    $tidyArgs = @('-p', $build, '-quiet')
    if ($Fix) { $tidyArgs += '-fix' }
    foreach ($header in $headers) {
        Write-Host "Checking header: $header"
        & clang-tidy @tidyArgs $header
        if ($LASTEXITCODE -ne 0) { Set-PhaseFailed }
    }
}

if ($fix) {
    if ($tidy) {
        if ($hasSources) {
            $phaseFailed = $false
            Invoke-WithSummary 'clang-tidy (fix)' { Invoke-TidyCheck $true }
        }
        if ($headers.Count) {
            $phaseFailed = $false
            Invoke-WithSummary 'clang-tidy headers (fix)' { Invoke-TidyHeaders $true }
        }
    }
    if ($format) {
        $phaseFailed = $false
        Invoke-WithSummary 'clang-format (fix)' { Invoke-Format $files $true }
    }
    if ($tidy) {
        if ($hasSources) {
            $phaseFailed = $false
            Invoke-WithSummary 'clang-tidy (check)' { Invoke-TidyCheck }
        }
        if ($headers.Count) {
            $phaseFailed = $false
            Invoke-WithSummary 'clang-tidy headers (check)' { Invoke-TidyHeaders }
        }
    }
} else {
    if ($format) {
        $phaseFailed = $false
        Invoke-WithSummary 'clang-format (check)' { Invoke-Format $files $false }
    }
    if ($tidy) {
        if ($hasSources) {
            $phaseFailed = $false
            Invoke-WithSummary 'clang-tidy (check)' { Invoke-TidyCheck }
        }
        if ($headers.Count) {
            $phaseFailed = $false
            Invoke-WithSummary 'clang-tidy headers (check)' { Invoke-TidyHeaders }
        }
    }
}
Write-Host "`nClang findings summary (emitted diagnostics):"
foreach ($summary in $summaries) {
    $suffix = if ($summary.External) { ", $($summary.External) external" } else { '' }
    if ($summary.Failed) { $suffix += ' (failed)' }
    Write-Host "  $($summary.Label): $($summary.Errors) errors, $($summary.Warnings) warnings, $($summary.Notes) notes$suffix"
}
exit $status
