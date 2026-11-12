[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_bt_bench"
$benchSource = Join-Path $repoRoot "Core/Src/sc_bt_bench.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-bt-bench-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_bt_bench-mutants-" + $runId)
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
    [Parameter(Mandatory = $true)][string]$BenchPath
  )

  $buildDirectory = Join-Path $buildRootResolved $Name
  Invoke-Native "cmake" -S $testSource -B $buildDirectory `
    -G $Generator -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$Compiler" `
    "-DSC_BT_BENCH_SOURCE=$BenchPath"
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
  $executable = Join-Path $BuildDirectory "sc_bt_bench_tests.exe"
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
  $testExitCode = $process.ExitCode
  $output = @($standardOutput, $standardError)
  Write-Host $standardOutput
  Write-Host $standardError

  if ($testExitCode -eq 0) {
    throw "mutant escaped detection: $TestName"
  }
  if (($output -join "`n") -notmatch "CHECK failed at ") {
    throw "mutant target did not execute a failing assertion: $TestName"
  }
}

function New-MutantSource {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement
  )

  $utf8 = [Text.UTF8Encoding]::new($false)
  $sourceText = [IO.File]::ReadAllText(
    $benchSource, $utf8).Replace("`r`n", "`n")
  $normalizedOriginal = $Original.Replace("`r`n", "`n")
  $normalizedReplacement = $Replacement.Replace("`r`n", "`n")
  if (-not $sourceText.Contains($normalizedOriginal)) {
    throw "mutation anchor not found for $Name"
  }

  $mutatedText = $sourceText.Replace(
    $normalizedOriginal, $normalizedReplacement)
  $mutantPath = Join-Path $temporaryRootResolved "$Name.c"
  [IO.File]::WriteAllText($mutantPath, $mutatedText, $utf8)
  return $mutantPath
}

function Invoke-Mutant {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  $mutant = New-MutantSource -Name $Name -Original $Original `
    -Replacement $Replacement
  $build = Build-TestExecutable -Name $Name -BenchPath $mutant
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName
  $script:mutantCount++
}

try {
  Write-Host "MUTANT SOURCE DIR: $temporaryRootResolved"
  Write-Host "MUTANT BUILD DIR: $buildRootResolved"

  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -BenchPath $benchSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "motor_bounds_rejected"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "unarmed_motion_safe_stop"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "auto_stop_deadline"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "max_line_exact"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "overlong_safe_stop"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "overlong_resync"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "invalid_byte_safe_stop"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "arm_tick_wrap"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "line_exact"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "line_alias_fragmented"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "line_does_not_extend_motion"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "line_invalid_snapshot_safe_stop"

  Invoke-Mutant -Name "limit_bypass" `
    -Original @'
  if ((!sc_bt_bench_parse_i32(tokens[2], -SC_BT_BENCH_MOTOR_MAX,
                              SC_BT_BENCH_MOTOR_MAX, &speed)) ||
'@ `
    -Replacement @'
  if ((!sc_bt_bench_parse_i32(tokens[2], -SC_BT_BENCH_MOTOR_MAX_COMMAND,
                              SC_BT_BENCH_MOTOR_MAX_COMMAND, &speed)) ||
'@ `
    -TestName "motor_bounds_rejected"

  Invoke-Mutant -Name "unarmed_execute" `
    -Original @'
  if ((s_armed == 0u) ||
      sc_bt_bench_time_reached(now_ms, s_arm_deadline_ms))
  {
    sc_bt_bench_command_error("ERR ARM\r\n");
    return;
  }
'@ `
    -Replacement @'
  if (0)
  {
    sc_bt_bench_command_error("ERR ARM\r\n");
    return;
  }
'@ `
    -TestName "unarmed_motion_safe_stop"

  Invoke-Mutant -Name "auto_stop_omission" `
    -Original @'
  if ((s_motor_active != 0u) &&
      sc_bt_bench_time_reached(now_ms, s_motor_deadline_ms))
  {
    sc_bt_bench_capture_sample(now_ms);
    stop_status = sc_bt_bench_safe_stop();
    if (stop_status == SC_TB6612_OK)
    {
      sc_bt_bench_send("OK STOP AUTO\r\n");
    }
    else
    {
      sc_bt_bench_send("ERR MOTOR\r\n");
    }
    activity = 1u;
  }
'@ `
    -Replacement @'
  if (0)
  {
    sc_bt_bench_capture_sample(now_ms);
    stop_status = sc_bt_bench_safe_stop();
    if (stop_status == SC_TB6612_OK)
    {
      sc_bt_bench_send("OK STOP AUTO\r\n");
    }
    else
    {
      sc_bt_bench_send("ERR MOTOR\r\n");
    }
    activity = 1u;
  }
'@ `
    -TestName "auto_stop_deadline"

  Invoke-Mutant -Name "unknown_no_stop" `
    -Original @'
  else
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
  }
}
'@ `
    -Replacement @'
  else
  {
    sc_bt_bench_send("ERR CMD\r\n");
  }
}
'@ `
    -TestName "max_line_exact"

  Invoke-Mutant -Name "overflow_no_stop" `
    -Original @'
    s_line_length = 0u;
    s_discarding = 1u;
    sc_bt_bench_command_error("ERR LINE\r\n");
'@ `
    -Replacement @'
    s_line_length = 0u;
    s_discarding = 1u;
    sc_bt_bench_send("ERR LINE\r\n");
'@ `
    -TestName "overlong_safe_stop"

  Invoke-Mutant -Name "invalid_byte_no_stop" `
    -Original @'
  if ((byte != (uint8_t)'\t') &&
      ((byte < (uint8_t)' ') || (byte > (uint8_t)'~')))
  {
    s_line_length = 0u;
    s_line[0] = '\0';
    s_discarding = 1u;
    sc_bt_bench_command_error("ERR CHAR\r\n");
    return;
  }
'@ `
    -Replacement @'
  if ((byte != (uint8_t)'\t') &&
      ((byte < (uint8_t)' ') || (byte > (uint8_t)'~')))
  {
    s_line_length = 0u;
    s_line[0] = '\0';
    s_discarding = 1u;
    sc_bt_bench_send("ERR CHAR\r\n");
    return;
  }
'@ `
    -TestName "invalid_byte_safe_stop"

  Invoke-Mutant -Name "arm_off_by_one" `
    -Original @'
  return (uint8_t)((uint32_t)(now_ms - deadline_ms) <=
                   (UINT32_MAX / 2u));
'@ `
    -Replacement @'
  return (uint8_t)((uint32_t)(now_ms - deadline_ms - 1u) <=
                   (UINT32_MAX / 2u));
'@ `
    -TestName "arm_tick_wrap"

  Invoke-Mutant -Name "overflow_no_discard" `
    -Original @'
    s_line_length = 0u;
    s_discarding = 1u;
    sc_bt_bench_command_error("ERR LINE\r\n");
'@ `
    -Replacement @'
    s_line_length = 0u;
    s_discarding = 0u;
    sc_bt_bench_command_error("ERR LINE\r\n");
'@ `
    -TestName "overlong_resync"

  Invoke-Mutant -Name "duration_limit_bypass" `
    -Original @'
      (!sc_bt_bench_parse_i32(tokens[3], SC_BT_BENCH_DURATION_MIN,
                              SC_BT_BENCH_DURATION_MAX, &duration)))
'@ `
    -Replacement @'
      (!sc_bt_bench_parse_i32(tokens[3], SC_BT_BENCH_DURATION_MIN,
                              10000, &duration)))
'@ `
    -TestName "motor_bounds_rejected"

  Invoke-Mutant -Name "line_bit_order" `
    -Original @'
  for (bit = 0; bit < 4; bit++)
'@ `
    -Replacement @'
  for (bit = 3; bit >= 0; bit--)
'@ `
    -TestName "line_exact"

  Invoke-Mutant -Name "line_alias_missing" `
    -Original @'
  else if ((sc_bt_bench_text_equal(tokens[0], "LINE")) ||
           (sc_bt_bench_text_equal(tokens[0], "LF")))
'@ `
    -Replacement @'
  else if (sc_bt_bench_text_equal(tokens[0], "LINE"))
'@ `
    -TestName "line_alias_fragmented"

  Invoke-Mutant -Name "line_query_safe_stop" `
    -Original @'
  if (sc_bt_bench_build_line(&line, now_ms) == 0u)
  {
    sc_bt_bench_command_error("ERR LINE\r\n");
    return;
  }
  sc_bt_bench_send(s_response);
}
'@ `
    -Replacement @'
  if (sc_bt_bench_build_line(&line, now_ms) == 0u)
  {
    sc_bt_bench_command_error("ERR LINE\r\n");
    return;
  }
  sc_bt_bench_command_error(s_response);
}
'@ `
    -TestName "line_does_not_extend_motion"

  Invoke-Mutant -Name "line_invalid_no_stop" `
    -Original @'
  if ((line.sample.valid == 0u) ||
      ((line.sample.raw_levels & 0xF0u) != 0u) ||
      (line.semantic_state == SC_LINE_INVALID))
  {
    sc_bt_bench_command_error("ERR IO\r\n");
    return;
  }
'@ `
    -Replacement @'
  if ((line.sample.valid == 0u) ||
      ((line.sample.raw_levels & 0xF0u) != 0u) ||
      (line.semantic_state == SC_LINE_INVALID))
  {
    sc_bt_bench_send("ERR IO\r\n");
    return;
  }
'@ `
    -TestName "line_invalid_snapshot_safe_stop"

  Invoke-Mutant -Name "raw_limit_bypass" `
    -Original @'
      (!sc_bt_bench_parse_i32(tokens[2], -SC_BT_BENCH_RAW_MAX,
                              SC_BT_BENCH_RAW_MAX, &pwm)) ||
'@ `
    -Replacement @'
      (!sc_bt_bench_parse_i32(tokens[2],
                              -SC_BT_BENCH_MOTOR_MAX_COMMAND,
                              SC_BT_BENCH_MOTOR_MAX_COMMAND, &pwm)) ||
'@ `
    -TestName "raw_requires_arm_and_limits"

  Invoke-Mutant -Name "speed_bypasses_arm" `
    -Original @'
  if ((s_armed == 0u) ||
      sc_bt_bench_time_reached(now_ms, s_arm_deadline_ms))
  {
    sc_bt_bench_command_error("ERR ARM\r\n");
    return;
  }
  if ((s_speed_ready == 0u) ||
'@ `
    -Replacement @'
  if (0)
  {
    sc_bt_bench_command_error("ERR ARM\r\n");
    return;
  }
  if ((s_speed_ready == 0u) ||
'@ `
    -TestName "speed_requires_arm_and_feedback"

  Invoke-Mutant -Name "pid_active_write" `
    -Original @'
  if ((s_motor_active != 0u) || (s_armed != 0u) ||
      (sc_tb6612_get_state(&tb_state) != SC_TB6612_OK) ||
      (tb_state.enabled != 0u) ||
      (tb_state.mode != SC_TB6612_MODE_STANDBY) ||
      (s_speed_controller.mode != SC_SPEED_CONTROLLER_MODE_STOPPED))
'@ `
    -Replacement @'
  (void)tb_state;
  if (0)
'@ `
    -TestName "pid_ram_readback_and_state_gate"

  Invoke-Mutant -Name "capture_io_error_ignored" `
    -Original @'
      if (status != SC_UART_OK)
      {
        s_last_fault_bits |= SC_BT_BENCH_FAULT_UART;
        sc_bt_bench_uart_fault();
        return sent;
      }
'@ `
    -Replacement @'
      if (0)
      {
        s_last_fault_bits |= SC_BT_BENCH_FAULT_UART;
        sc_bt_bench_uart_fault();
        return sent;
      }
'@ `
    -TestName "capture_requires_stopped_and_uart_error_safe"

  Invoke-Mutant -Name "capture_tx_full_drops_frame" `
    -Original @'
      if ((status == SC_UART_ERR_BUFFER_FULL) ||
          (status == SC_UART_ERR_BUSY))
      {
        return sent;
      }
'@ `
    -Replacement @'
      if (0)
      {
        return sent;
      }
'@ `
    -TestName "capture_tx_full_retries"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-bt-bench-mutants-",
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
        "sc_bt_bench-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
  else {
    Write-Warning "mutation build directory was not removed: $resolvedMutationBuildRoot"
  }
}
