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
                '' \
                'Options:' \
                '  -h, --help                         Show this help and exit.' \
                '  --fix                              Apply fixes with the selected tools.' \
                '  --format                           Run clang-format only (unless --tidy is also set).' \
                '  --tidy                             Run clang-tidy only (unless --format is also set).' \
                '  -BuildDirectory, --build-dir DIR   Compilation database directory (default: build/Release).' \
                '  -Jobs, --jobs N                    Read-only run-clang-tidy workers (default: up to 4).' \
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
if $tidy; then
    first_source="$(find "${paths[@]}" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \) -print | head -n 1)"
    if [[ -z $first_source ]]; then
        echo 'run-clang-tidy requires a selected source file with a compilation database entry; headers are checked through selected sources.' >&2
        exit 1
    fi
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

summary_dir=
cleanup_summary() {
    if [[ -n $summary_dir ]]; then
        rm -f -- "$summary_dir/stdout" "$summary_dir/stderr"
        rmdir -- "$summary_dir"
    fi
}
trap cleanup_summary EXIT

summarize_stream() {
    awk -v counts="$1" '
        {
            print
            fflush()
            line = $0
            gsub(sprintf("%c\\[[0-9;]*m", 27), "", line)
            if (line ~ /^((.+:[0-9]+(:[0-9]+)?:[[:space:]]*)|(clang(-tidy|-format)?(\.exe)?:[[:space:]]*)?)(fatal error|error|warning|note):/) {
                if (line ~ /(^|:[[:space:]]*)(fatal error|error):/) errors++
                else if (line ~ /(^|:[[:space:]]*)warning:/) warnings++
                else notes++
            }
        }
        END { printf "%d %d %d\n", errors, warnings, notes > counts }
    '
}

summaries=()
run_with_summary() {
    local label=$1 result out_pid err_pid
    local out_errors out_warnings out_notes err_errors err_warnings err_notes
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
    if ! read -r out_errors out_warnings out_notes < "$summary_dir/stdout" ||
       ! read -r err_errors err_warnings err_notes < "$summary_dir/stderr"; then
        echo 'Could not collect diagnostic counts.' >&2
        cleanup_summary
        summary_dir=
        return 1
    fi
    cleanup_summary
    summary_dir=
    local suffix=
    if ((result != 0)); then suffix=' (failed)'; fi
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
        run_with_summary 'clang-tidy (fix)' tidy_check -fix || status=1
    fi
    if $format; then
        run_with_summary 'clang-format (fix)' format_check fix || status=1
    fi
    if $tidy; then run_with_summary 'clang-tidy (check)' tidy_check || status=1; fi
else
    if $format; then
        run_with_summary 'clang-format (check)' format_check check || status=1
    fi
    if $tidy; then run_with_summary 'clang-tidy (check)' tidy_check || status=1; fi
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

Options:
  -h, --help                         Show this help and exit.
  --fix                              Apply fixes with the selected tools.
  --format                           Run clang-format only (unless --tidy is also set).
  --tidy                             Run clang-tidy only (unless --format is also set).
  -BuildDirectory, --build-dir DIR   Compilation database directory (default: build\Release).
  -Jobs, --jobs N                    Read-only run-clang-tidy workers (default: up to 4).
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
if ($tidy) {
    if (!@($files | Where-Object { [IO.Path]::GetExtension($_) -cin '.c', '.cc', '.cpp', '.cxx' }).Count) {
        throw 'run-clang-tidy requires a selected source file with a compilation database entry; headers are checked through selected sources.'
    }
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
function Invoke-WithSummary {
    param([string]$Label, [scriptblock]$Command)
    $summary = [PSCustomObject]@{ Label = $Label; Errors = 0; Warnings = 0; Notes = 0; Failed = $false }
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
        if ($_ -is [System.Management.Automation.ErrorRecord]) {
            [Console]::Error.WriteLine($line)
        } else {
            Write-Output $_
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
    if (!(Get-Command run-clang-tidy -ErrorAction SilentlyContinue)) {
        Write-Error 'run-clang-tidy is required on PATH.' -ErrorAction Continue
        Set-PhaseFailed
        return
    }
    & run-clang-tidy @tidyArgs @tidyPatterns
    if ($LASTEXITCODE -ne 0) { Set-PhaseFailed }
}

if ($fix) {
    if ($tidy) {
        $phaseFailed = $false
        Invoke-WithSummary 'clang-tidy (fix)' { Invoke-TidyCheck $true }
    }
    if ($format) {
        $phaseFailed = $false
        Invoke-WithSummary 'clang-format (fix)' { Invoke-Format $files $true }
    }
    if ($tidy) {
        $phaseFailed = $false
        Invoke-WithSummary 'clang-tidy (check)' { Invoke-TidyCheck }
    }
} else {
    if ($format) {
        $phaseFailed = $false
        Invoke-WithSummary 'clang-format (check)' { Invoke-Format $files $false }
    }
    if ($tidy) {
        $phaseFailed = $false
        Invoke-WithSummary 'clang-tidy (check)' { Invoke-TidyCheck }
    }
}
Write-Host "`nClang findings summary (emitted diagnostics):"
foreach ($summary in $summaries) {
    $suffix = if ($summary.Failed) { ' (failed)' } else { '' }
    Write-Host "  $($summary.Label): $($summary.Errors) errors, $($summary.Warnings) warnings, $($summary.Notes) notes$suffix"
}
exit $status
