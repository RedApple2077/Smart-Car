[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_track"
$productionSource = Join-Path $repoRoot "Core/Src/sc_track.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-track-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_track-mutants-" + $runId)
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
    [Parameter(Mandatory = $true)][string]$SourcePath
  )

  $buildDirectory = Join-Path $buildRootResolved $Name
  Invoke-Native "cmake" -S $testSource -B $buildDirectory `
    -G $Generator -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$Compiler" "-DSC_TRACK_SOURCE=$SourcePath"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName,
    [string]$ExpectedExpression
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_track_tests.exe"
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
  Write-Host $standardOutput
  Write-Host $standardError
  if ($process.ExitCode -eq 0) {
    throw "mutant escaped detection: $TestName"
  }
  $failurePattern = "CHECK failed at "
  if (-not [string]::IsNullOrWhiteSpace($ExpectedExpression)) {
    $failurePattern += ".*: " + [regex]::Escape($ExpectedExpression)
  }
  if (($standardOutput + "`n" + $standardError) -notmatch
      $failurePattern) {
    throw "mutant target did not execute a failing assertion: $TestName"
  }
}

function Invoke-Mutant {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Replacement,
    [Parameter(Mandatory = $true)][string]$TestName,
    [string]$ExpectedExpression
  )

  $utf8 = [Text.UTF8Encoding]::new($false)
  $sourceText = [IO.File]::ReadAllText($productionSource, $utf8)
  if (-not $sourceText.Contains($Original)) {
    throw "mutation anchor not found for $Name"
  }
  $mutantPath = Join-Path $temporaryRootResolved "$Name.c"
  [IO.File]::WriteAllText(
    $mutantPath, $sourceText.Replace($Original, $Replacement), $utf8)
  $build = Build-TestExecutable -Name $Name -SourcePath $mutantPath
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName `
    -ExpectedExpression $ExpectedExpression
  $script:mutantCount++
}

try {
  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -SourcePath $productionSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-Mutant -Name "single_sample_confirms" `
    -Original @'
  if ((controller->recovery_sample_count >= SC_TRACK_RECOVERY_SAMPLES) &&
      ((uint32_t)(line->sample.sample_time_ms -
                  controller->recovery_first_sample_time_ms) >=
       SC_TRACK_RECOVERY_MIN_TIME_MS))
'@ `
    -Replacement @'
  if (fresh != 0u)
'@ `
    -TestName "single_black_sample_does_not_reset_recovery"

  Invoke-Mutant -Name "all_white_blind_forward" `
    -Original @'
  else
  {
    sc_track_clear_target(target_out);
    return SC_TRACK_ERR_LOST;
  }
  return SC_TRACK_RECOVERING;
'@ `
    -Replacement @'
  else
  {
    sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                        SC_TRACK_CENTER_RATIO, target_out);
  }
  return SC_TRACK_RECOVERING;
'@ `
    -TestName "unconditional_all_white_stop"

  Invoke-Mutant -Name "wrong_fine_ratio" `
    -Original "#define SC_TRACK_FINE_INNER_RATIO     850" `
    -Replacement "#define SC_TRACK_FINE_INNER_RATIO     145" `
    -TestName "ratio_table"

  Invoke-Mutant -Name "fine_steering_regression" `
    -Original "#define SC_TRACK_FINE_INNER_RATIO     850" `
    -Replacement "#define SC_TRACK_FINE_INNER_RATIO     558" `
    -TestName "fine_steering_is_conservative"

  Invoke-Mutant -Name "ordinary_lost_timeout_reenabled" `
    -Original "#define SC_TRACK_LOST_RECOVERY_MS     0u" `
    -Replacement "#define SC_TRACK_LOST_RECOVERY_MS     1000u" `
    -TestName "lost_search_without_timeout"

  Invoke-Mutant -Name "medium_outer_old_ratio" `
    -Original "#define SC_TRACK_MEDIUM_OUTER_RATIO   900" `
    -Replacement "#define SC_TRACK_MEDIUM_OUTER_RATIO   1386" `
    -TestName "severe_steering_derates_and_does_not_overspeed"

  Invoke-Mutant -Name "turn_outer_old_ratio" `
    -Original "#define SC_TRACK_TURN_OUTER_RATIO     900" `
    -Replacement "#define SC_TRACK_TURN_OUTER_RATIO     1424" `
    -TestName "severe_steering_derates_and_does_not_overspeed"

  Invoke-Mutant -Name "search_outer_old_ratio" `
    -Original "#define SC_TRACK_SEARCH_OUTER_RATIO   900" `
    -Replacement "#define SC_TRACK_SEARCH_OUTER_RATIO   1194" `
    -TestName "severe_steering_derates_and_does_not_overspeed"

  Invoke-Mutant -Name "center_keeps_direction" `
    -Original @'
      sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                          SC_TRACK_CENTER_RATIO, target_out);
      controller->last_turn_direction = 0;
      return SC_TRACK_OK;
'@ `
    -Replacement @'
      sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                          SC_TRACK_CENTER_RATIO, target_out);
      return SC_TRACK_OK;
'@ `
    -TestName "center_clears_direction"

  Invoke-Mutant -Name "junction_mask_missing" `
    -Original @'
  return (uint8_t)(
    (black_mask == 0x07u) || (black_mask == 0x0Bu) ||
    (black_mask == 0x0Du) || (black_mask == 0x0Eu) ||
    (black_mask == 0x0Fu));
'@ `
    -Replacement @'
  return (uint8_t)(
    (black_mask == 0x0Bu) || (black_mask == 0x0Du) ||
    (black_mask == 0x0Eu) || (black_mask == 0x0Fu));
'@ `
    -TestName "all_junction_masks_enter"

  Invoke-Mutant -Name "single_junction_confirms" `
    -Original "#define SC_TRACK_CROSSING_ENTER_SAMPLES 3u" `
    -Replacement "#define SC_TRACK_CROSSING_ENTER_SAMPLES 1u" `
    -TestName "single_junction_does_not_enter"

  Invoke-Mutant -Name "junction_sequence_not_fresh" `
    -Original @'
  uint8_t fresh =
    (uint8_t)((controller->crossing_entry_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->crossing_entry_last_sequence));
'@ `
    -Replacement "  uint8_t fresh = 1u;" `
    -TestName "junction_entry_requires_three_fresh_samples"

  Invoke-Mutant -Name "junction_gap_not_checked" `
    -Original "#define SC_TRACK_CROSSING_MAX_GAP_MS    30u" `
    -Replacement "#define SC_TRACK_CROSSING_MAX_GAP_MS    1000u" `
    -TestName "junction_entry_requires_three_fresh_samples"

  Invoke-Mutant -Name "crossing_nonjunction_exits_early" `
    -Original @'
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <
        SC_TRACK_CROSSING_MAX_MS)
'@ `
    -Replacement @'
    if ((line->black_mask != 0x06u) &&
        (line->black_mask != 0x00u))
    {
      sc_track_reset_crossing(controller);
      return sc_track_follow_ordinary(controller, line->black_mask,
                                      target_out);
    }
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <
        SC_TRACK_CROSSING_MAX_MS)
'@ `
    -TestName "crossing_bridges_short_nonjunction_samples"

  Invoke-Mutant -Name "crossing_white_stops_early" `
    -Original @'
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <
        SC_TRACK_CROSSING_MAX_MS)
'@ `
    -Replacement @'
    if (line->black_mask == 0x00u)
    {
      sc_track_reset_crossing(controller);
      return SC_TRACK_ERR_LOST;
    }
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <
        SC_TRACK_CROSSING_MAX_MS)
'@ `
    -TestName "crossing_bridges_short_nonjunction_samples"

  Invoke-Mutant -Name "crossing_deadline_renews" `
    -Original @'
      else if (sc_track_is_junction(line->black_mask) != 0u)
      {
        sc_track_reset_crossing_center_candidate(controller);
        sc_track_reset_crossing_ordinary_candidate(controller);
      }
'@ `
    -Replacement @'
      else if (sc_track_is_junction(line->black_mask) != 0u)
      {
        controller->crossing_start_ms = now_ms;
        sc_track_reset_crossing_center_candidate(controller);
        sc_track_reset_crossing_ordinary_candidate(controller);
      }
'@ `
    -TestName "crossing_persistent_junction_times_out_with_cooldown"

  Invoke-Mutant -Name "center_exit_after_one" `
    -Original "#define SC_TRACK_CROSSING_EXIT_SAMPLES  3u" `
    -Replacement "#define SC_TRACK_CROSSING_EXIT_SAMPLES  1u" `
    -TestName "crossing_exits_on_three_center_samples"

  Invoke-Mutant -Name "center_duplicate_sequence_counts" `
    -Original @'
  uint8_t fresh =
    (uint8_t)((controller->crossing_center_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->crossing_center_last_sequence));
'@ `
    -Replacement "  uint8_t fresh = 1u;" `
    -TestName "crossing_exits_on_three_center_samples"

  Invoke-Mutant -Name "center_gap_not_checked" `
    -Original @'
  if (controller->crossing_center_sample_count != 0u)
  {
    sample_gap_ms =
      (uint32_t)(line->sample.sample_time_ms -
                 controller->crossing_center_last_sample_time_ms);
    if ((sample_gap_ms == 0u) ||
        (sample_gap_ms > SC_TRACK_CROSSING_MAX_GAP_MS))
'@ `
    -Replacement @'
  if (controller->crossing_center_sample_count != 0u)
  {
    sample_gap_ms =
      (uint32_t)(line->sample.sample_time_ms -
                 controller->crossing_center_last_sample_time_ms);
    if ((sample_gap_ms == 0u) ||
        (sample_gap_ms > 1000u))
'@ `
    -TestName "crossing_exits_on_three_center_samples"

  Invoke-Mutant -Name "crossing_timeout_ignored" `
    -Original "#define SC_TRACK_CROSSING_MAX_MS        500u" `
    -Replacement "#define SC_TRACK_CROSSING_MAX_MS        5000u" `
    -TestName "crossing_persistent_junction_times_out_with_cooldown"

  Invoke-Mutant -Name "crossing_white_window_never_stops" `
    -Original "#define SC_TRACK_CROSSING_WHITE_MAX_MS  100u" `
    -Replacement "#define SC_TRACK_CROSSING_WHITE_MAX_MS  1000u" `
    -TestName "crossing_white_window_stops"

  Invoke-Mutant -Name "crossing_timeout_ordinary_restores" `
    -Original @'
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <
        SC_TRACK_CROSSING_MAX_MS)
    {
      return sc_track_output_crossing(controller, target_out);
    }

    return sc_track_crossing_lost(controller, now_ms, target_out);
'@ `
    -Replacement @'
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <
        SC_TRACK_CROSSING_MAX_MS)
    {
      return sc_track_output_crossing(controller, target_out);
    }

    sc_track_reset_crossing(controller);
    sc_track_clear_crossing_cooldown(controller);
    return sc_track_follow_ordinary(controller, line->black_mask,
                                    target_out);
'@ `
    -TestName "crossing_timeout_single_ordinary_stops"

  Invoke-Mutant -Name "crossing_reentry_cooldown_ignored" `
    -Original "  if (controller->crossing_cooldown_active != 0u)" `
    -Replacement "  if (0)" `
    -TestName "crossing_persistent_junction_times_out_with_cooldown"

  Invoke-Mutant -Name "crossing_ordinary_exit_ignored" `
    -Original "#define SC_TRACK_CROSSING_EXIT_SAMPLES  3u" `
    -Replacement "#define SC_TRACK_CROSSING_EXIT_SAMPLES  100u" `
    -TestName "crossing_ordinary_line_exits_after_three"

  Invoke-Mutant -Name "crossing_ordinary_sequence_not_fresh" `
    -Original @'
  uint8_t fresh =
    (uint8_t)((controller->crossing_ordinary_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->crossing_ordinary_last_sequence));
'@ `
    -Replacement "  uint8_t fresh = 1u;" `
    -TestName "crossing_ordinary_candidate_rejects_gap_and_duplicate"

  Invoke-Mutant -Name "crossing_ordinary_mask_change_ignored" `
    -Original @'
  if (controller->crossing_ordinary_sample_count != 0u)
  {
    if (line->black_mask != controller->crossing_ordinary_last_mask)
    {
      sc_track_reset_crossing_ordinary_candidate(controller);
      return 0u;
    }
    sample_gap_ms =
'@ `
    -Replacement @'
  if (controller->crossing_ordinary_sample_count != 0u)
  {
    sample_gap_ms =
'@ `
    -TestName "crossing_alternating_ordinary_masks_do_not_exit"

  Invoke-Mutant -Name "crossing_ordinary_gap_not_checked" `
    -Original @'
    if (line->black_mask != controller->crossing_ordinary_last_mask)
    {
      sc_track_reset_crossing_ordinary_candidate(controller);
      return 0u;
    }
    sample_gap_ms =
      (uint32_t)(line->sample.sample_time_ms -
                 controller->crossing_ordinary_last_sample_time_ms);
    if ((sample_gap_ms == 0u) ||
        (sample_gap_ms > SC_TRACK_CROSSING_MAX_GAP_MS))
'@ `
    -Replacement @'
    if (line->black_mask != controller->crossing_ordinary_last_mask)
    {
      sc_track_reset_crossing_ordinary_candidate(controller);
      return 0u;
    }
    sample_gap_ms =
      (uint32_t)(line->sample.sample_time_ms -
                 controller->crossing_ordinary_last_sample_time_ms);
    if ((sample_gap_ms == 0u) ||
        (sample_gap_ms > 1000u))
'@ `
    -TestName "crossing_ordinary_candidate_rejects_gap_and_duplicate"

  Invoke-Mutant -Name "junction_candidate_turns" `
    -Original @'
  if (sc_track_is_junction(line->black_mask) != 0u)
  {
    if (sc_track_crossing_entry_update(controller, line, now_ms) != 0u)
    {
      return sc_track_output_crossing(controller, target_out);
    }
    return sc_track_output_crossing(controller, target_out);
  }
'@ `
    -Replacement @'
  if (sc_track_is_junction(line->black_mask) != 0u)
  {
    if (sc_track_crossing_entry_update(controller, line, now_ms) != 0u)
    {
      return sc_track_output_crossing(controller, target_out);
    }
    return sc_track_follow_ordinary(controller, line->black_mask,
                                    target_out);
  }
'@ `
    -TestName "single_junction_does_not_enter"

  Invoke-Mutant -Name "invalid_keeps_crossing" `
    -Original @'
  if (line->sample.valid == 0u)
  {
    sc_track_reset_crossing(controller);
    sc_track_reset_recovery_candidate(controller);
'@ `
    -Replacement @'
  if (line->sample.valid == 0u)
  {
    sc_track_reset_recovery_candidate(controller);
'@ `
    -TestName "invalid_sample_clears_crossing"

  Invoke-Mutant -Name "invalid_high_bits_keep_crossing" `
    -Original @'
  if ((line->black_mask & 0xF0u) != 0u)
  {
    sc_track_reset_crossing(controller);
    sc_track_reset_recovery_candidate(controller);
'@ `
    -Replacement @'
  if ((line->black_mask & 0xF0u) != 0u)
  {
    sc_track_reset_recovery_candidate(controller);
'@ `
    -TestName "invalid_sample_clears_crossing"

  Invoke-Mutant -Name "recovery_ignores_minimum_time" `
    -Original "#define SC_TRACK_RECOVERY_MIN_TIME_MS 20u" `
    -Replacement "#define SC_TRACK_RECOVERY_MIN_TIME_MS 1u" `
    -TestName "three_fresh_samples_and_minimum_time_confirm"

  Invoke-Mutant -Name "duplicate_sequence_counts" `
    -Original @'
  uint8_t fresh =
    (uint8_t)((controller->recovery_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->recovery_last_sequence));
'@ `
    -Replacement "  uint8_t fresh = 1u;" `
    -TestName "duplicate_sequence_does_not_confirm"

  Invoke-Mutant -Name "white_keeps_recovery_candidate" `
    -Original @'
    if (line->black_mask == 0x00u)
    {
      sc_track_reset_recovery_candidate(controller);
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -Replacement @'
    if (line->black_mask == 0x00u)
    {
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -TestName "alternating_white_black_without_timeout"

  Invoke-Mutant -Name "recovering_junction_not_excluded" `
    -Original @'
    if ((controller->last_turn_direction != 0) &&
        (sc_track_is_junction(line->black_mask) != 0u))
    {
      sc_track_reset_recovery_candidate(controller);
      return sc_track_output_search(controller, target_out);
    }

'@ `
    -Replacement "" `
    -TestName "recovering_junction_masks_excluded" `
    -ExpectedExpression "controller.recovery_sample_count == 0u"

  Invoke-Mutant -Name "recovering_full_black_timeline_not_excluded" `
    -Original @'
    if ((controller->last_turn_direction != 0) &&
        (sc_track_is_junction(line->black_mask) != 0u))
    {
      sc_track_reset_recovery_candidate(controller);
      return sc_track_output_search(controller, target_out);
    }

'@ `
    -Replacement "" `
    -TestName "recovering_full_black_timeline_endpoint" `
    -ExpectedExpression "(last_now_ms == 11530u) && (final_status == SC_TRACK_RECOVERING)"

  Invoke-Mutant -Name "recovering_junction_keeps_candidate" `
    -Original @'
    if ((controller->last_turn_direction != 0) &&
        (sc_track_is_junction(line->black_mask) != 0u))
    {
      sc_track_reset_recovery_candidate(controller);
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -Replacement @'
    if ((controller->last_turn_direction != 0) &&
        (sc_track_is_junction(line->black_mask) != 0u))
    {
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -TestName "recovering_junction_masks_excluded" `
    -ExpectedExpression "controller.recovery_sample_count == 0u"

  Invoke-Mutant -Name "recovering_junction_partial_exclusion" `
    -Original @'
    if ((controller->last_turn_direction != 0) &&
        (sc_track_is_junction(line->black_mask) != 0u))
'@ `
    -Replacement @'
    if ((controller->last_turn_direction != 0) &&
        (line->black_mask == 0x0Fu))
'@ `
    -TestName "recovering_junction_masks_excluded" `
    -ExpectedExpression "controller.recovery_sample_count == 0u"

  Invoke-Mutant -Name "recovering_junction_clears_direction" `
    -Original @'
    if ((controller->last_turn_direction != 0) &&
        (sc_track_is_junction(line->black_mask) != 0u))
    {
      sc_track_reset_recovery_candidate(controller);
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -Replacement @'
    if ((controller->last_turn_direction != 0) &&
        (sc_track_is_junction(line->black_mask) != 0u))
    {
      sc_track_reset_recovery_candidate(controller);
      controller->last_turn_direction = 0;
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -TestName "recovering_junction_masks_excluded" `
    -ExpectedExpression "step(&controller, &line, 30u, &target) == SC_TRACK_RECOVERING"

  Invoke-Mutant -Name "mixed_recovery_masks_rejected" `
    -Original @'
static uint8_t sc_track_recovery_confirm(
  sc_track_controller_t *controller, const sc_line_state_t *line)
{
  uint8_t fresh =
    (uint8_t)((controller->recovery_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->recovery_last_sequence));
'@ `
    -Replacement @'
static uint8_t sc_track_recovery_confirm(
  sc_track_controller_t *controller, const sc_line_state_t *line)
{
  uint8_t fresh =
    (uint8_t)((controller->recovery_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->recovery_last_sequence));

  if (line->black_mask != 0x06u)
  {
    sc_track_reset_recovery_candidate(controller);
    return 0u;
  }
'@ `
    -TestName "recovery_mixed_ordinary_masks_confirm" `
    -ExpectedExpression "controller.recovery_sample_count == 2u"

  Invoke-Mutant -Name "recovery_rejects_wrapped_sample_time" `
    -Original @'
    sample_gap_ms =
      (uint32_t)(line->sample.sample_time_ms -
                 controller->recovery_last_sample_time_ms);
'@ `
    -Replacement @'
    sample_gap_ms =
      (line->sample.sample_time_ms >=
       controller->recovery_last_sample_time_ms) ?
      (uint32_t)(line->sample.sample_time_ms -
                 controller->recovery_last_sample_time_ms) :
      UINT32_MAX;
'@ `
    -TestName "recovery_candidate_window_and_wrap" `
    -ExpectedExpression "time_wrap_status == SC_TRACK_OK"

  Invoke-Mutant -Name "crossing_deadline_inclusive" `
    -Original @'
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <
        SC_TRACK_CROSSING_MAX_MS)
'@ `
    -Replacement @'
    if ((uint32_t)(now_ms - controller->crossing_start_ms) <=
        SC_TRACK_CROSSING_MAX_MS)
'@ `
    -TestName "crossing_deadline_and_center_priority" `
    -ExpectedExpression "step(&controller, &line, 600u, &target) == SC_TRACK_ERR_LOST"

  Invoke-Mutant -Name "crossing_deadline_checked_before_center" `
    -Original @'
  if (controller->crossing_active != 0u)
  {
    uint8_t center_confirmed = 0u;
    uint8_t ordinary_confirmed = 0u;

    if (line->black_mask == 0x00u)
'@ `
    -Replacement @'
  if (controller->crossing_active != 0u)
  {
    uint8_t center_confirmed = 0u;
    uint8_t ordinary_confirmed = 0u;

    if ((uint32_t)(now_ms - controller->crossing_start_ms) >=
        SC_TRACK_CROSSING_MAX_MS)
    {
      return sc_track_crossing_lost(controller, now_ms, target_out);
    }

    if (line->black_mask == 0x00u)
'@ `
    -TestName "crossing_deadline_and_center_priority" `
    -ExpectedExpression "step(&controller, &line, 600u, &target) == SC_TRACK_OK"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-track-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot =
    [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_track-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
}
