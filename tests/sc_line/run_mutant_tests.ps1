[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_line"
$lfSource = Join-Path $repoRoot "Core/Src/sc_lf04.c"
$lineSource = Join-Path $repoRoot "Core/Src/sc_line_service.c"
$benchSource = Join-Path $repoRoot "Core/Src/sc_bt_bench.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-line-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_line-mutants-" + $runId)
$buildRootResolved =
  (New-Item -ItemType Directory -Path $buildRoot).FullName
$mutantCount = 0

function Invoke-Native {
  param(
    [Parameter(Mandatory = $true)][string]$FilePath,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments
  )

  & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw ("command failed with exit code {0}: {1} {2}" -f
      $LASTEXITCODE, $FilePath, ($Arguments -join " "))
  }
}

function Build-TestExecutable {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$LfPath,
    [Parameter(Mandatory = $true)][string]$LinePath
  )

  $buildDirectory = Join-Path $buildRootResolved $Name
  Invoke-Native "cmake" -S $testSource -B $buildDirectory `
    -G $Generator -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$Compiler" `
    "-DSC_LF04_SOURCE=$LfPath" `
    "-DSC_LINE_SERVICE_SOURCE=$LinePath" `
    "-DSC_BT_BENCH_SOURCE=$benchSource"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedPass {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT PASS: $TestName"
  Invoke-Native "ctest" --test-dir $BuildDirectory `
    -R "^$TestName$" --output-on-failure
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_line_tests.exe"
  if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "test executable not found: $executable"
  }

  $startInfo = [Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $executable
  $startInfo.ArgumentList.Add($TestName)
  $startInfo.UseShellExecute = $false
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $startInfo.CreateNoWindow = $true

  $process = [Diagnostics.Process]::Start($startInfo)
  $standardOutput = $process.StandardOutput.ReadToEnd()
  $standardError = $process.StandardError.ReadToEnd()
  $process.WaitForExit()
  $output = @($standardOutput, $standardError)
  Write-Host $standardOutput
  Write-Host $standardError

  if ($process.ExitCode -eq 0) {
    throw "mutant escaped detection: $TestName"
  }
  if (($output -join "`n") -notmatch "CHECK failed at ") {
    throw "mutant target did not execute a failing assertion: $TestName"
  }
}

function New-MutantSource {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement
  )

  $utf8 = [Text.UTF8Encoding]::new($false)
  $sourceText = [IO.File]::ReadAllText($SourcePath, $utf8)
  if (-not $sourceText.Contains($Original)) {
    throw "mutation anchor not found for $Name"
  }
  $mutatedText = $sourceText.Replace($Original, $Replacement)
  $mutantPath = Join-Path $temporaryRootResolved "$Name.c"
  [IO.File]::WriteAllText($mutantPath, $mutatedText, $utf8)
  return $mutantPath
}

function Invoke-Mutant {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  $mutant = New-MutantSource -Name $Name -SourcePath $SourcePath `
    -Original $Original -Replacement $Replacement
  if ($SourcePath -eq $lfSource) {
    $build = Build-TestExecutable -Name $Name -LfPath $mutant `
      -LinePath $lineSource
  }
  else {
    $build = Build-TestExecutable -Name $Name -LfPath $lfSource `
      -LinePath $mutant
  }
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName
  $script:mutantCount++
}

try {
  Write-Host "MUTANT SOURCE DIR: $temporaryRootResolved"
  Write-Host "MUTANT BUILD DIR: $buildRootResolved"

  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -LfPath $lfSource -LinePath $lineSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "lf_config_rejections"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "lf_sample_snapshot_sequence"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "line_all_sixteen_patterns"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "line_invalid_and_argument"

  Invoke-Mutant -Name "lf_bit_order" -SourcePath $lfSource `
    -Original "s_latest.raw_levels = (uint8_t)((idr >> 12u) & 0x0Fu);" `
    -Replacement "s_latest.raw_levels = (uint8_t)((((idr >> 12u) & 0x1u) << 3u) | (((idr >> 13u) & 0x1u) << 2u) | (((idr >> 14u) & 0x1u) << 1u) | ((idr >> 15u) & 0x1u));" `
    -TestName "lf_sample_snapshot_sequence"

  Invoke-Mutant -Name "lf_config_crh_ignored" -SourcePath $lfSource `
    -Original "return (uint8_t)(((GPIOB->CRH >> shift) & 0x0Fu) == 0x8u);" `
    -Replacement "return (uint8_t)((((GPIOB->CRH >> shift) & 0x0Fu) == 0x8u) + 1u);" `
    -TestName "lf_config_rejections"

  Invoke-Mutant -Name "lf_config_pull_ignored" -SourcePath $lfSource `
    -Original "return (uint8_t)((GPIOB->ODR & 0xF000u) == 0xF000u);" `
    -Replacement "return 1u;" `
    -TestName "lf_config_rejections"

  Invoke-Mutant -Name "lf_failure_advances_sequence" -SourcePath $lfSource `
    -Original "  s_state.sample_failures++;" `
    -Replacement "  s_state.sample_failures++;`n  s_latest.sequence++;" `
    -TestName "lf_config_failure_recovery"

  Invoke-Mutant -Name "line_mask_not_inverted" -SourcePath $lineSource `
    -Original "s_latest.black_mask = (uint8_t)(~sample->raw_levels) & 0x0Fu;" `
    -Replacement "s_latest.black_mask = sample->raw_levels & 0x0Fu;" `
    -TestName "line_all_sixteen_patterns"

  Invoke-Mutant -Name "line_left_right_swap" -SourcePath $lineSource `
    -Original "    case 0x07u:`n      return SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE;" `
    -Replacement "    case 0x07u:`n      return SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE;" `
    -TestName "line_all_sixteen_patterns"

  Invoke-Mutant -Name "line_junction_lost_merge" -SourcePath $lineSource `
    -Original "    case 0x0Fu:`n      return SC_LINE_JUNCTION_CANDIDATE;" `
    -Replacement "    case 0x0Fu:`n      return SC_LINE_LOST_CANDIDATE;" `
    -TestName "line_all_sixteen_patterns"

  Invoke-Mutant -Name "line_unknown_invalid" -SourcePath $lineSource `
    -Original "    default:`n      return SC_LINE_UNKNOWN_PATTERN;" `
    -Replacement "    default:`n      return SC_LINE_INVALID;" `
    -TestName "line_all_sixteen_patterns"

  Invoke-Mutant -Name "line_valid_ignored" -SourcePath $lineSource `
    -Original "if ((sample->valid == 0u) || ((sample->raw_levels & 0xF0u) != 0u))" `
    -Replacement "if (((sample->raw_levels & 0xF0u) != 0u))" `
    -TestName "line_invalid_and_argument"

  Invoke-Mutant -Name "line_invalid_retains_old" -SourcePath $lineSource `
    -Original "    s_latest.sample.valid = 0u;`n    s_latest.black_mask = 0u;`n    s_latest.semantic_state = SC_LINE_INVALID;`n    return SC_LINE_ERR_INVALID_DATA;" `
    -Replacement "    return SC_LINE_ERR_INVALID_DATA;" `
    -TestName "line_invalid_and_argument"

  Invoke-Mutant -Name "line_high_bits_truncated" -SourcePath $lineSource `
    -Original "((sample->raw_levels & 0xF0u) != 0u)" `
    -Replacement "0u" `
    -TestName "line_invalid_and_argument"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-line-mutants-",
        [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }
  else {
    Write-Warning "temporary mutation directory was not removed: $resolvedMutationRoot"
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot =
    [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_line-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
  else {
    Write-Warning "mutation build directory was not removed: $resolvedMutationBuildRoot"
  }
}
