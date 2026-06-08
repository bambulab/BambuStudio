param(
  [string]$EventName = $env:GITHUB_EVENT_NAME,
  [string]$PullRequestBaseSha = $env:PR_BASE_SHA,
  [string]$BeforeSha = $env:BEFORE_SHA,
  [string]$HeadSha = $env:GITHUB_SHA,
  [string]$OutputPath = $env:GITHUB_OUTPUT,
  [switch]$SkipFetch
)

$ErrorActionPreference = 'Stop'

$scopeDefs = @(
  @{ Key = 'config'; Output = 'config'; Patterns = '.github/config-fast-gate-paths.txt'; Label = 'config gate' },
  @{ Key = 'placeholder_parser'; Output = 'placeholder_parser'; Patterns = '.github/placeholder-parser-smoke-paths.txt'; Label = 'placeholder parser smoke gate' },
  @{ Key = 'gcodewriter'; Output = 'gcodewriter'; Patterns = '.github/gcodewriter-smoke-paths.txt'; Label = 'gcodewriter smoke gate' },
  @{ Key = 'gcode_reader'; Output = 'gcode_reader'; Patterns = '.github/gcode-reader-smoke-paths.txt'; Label = 'gcode reader smoke gate' },
  @{ Key = 'model_assembly'; Output = 'model_assembly'; Patterns = '.github/model-assembly-smoke-paths.txt'; Label = 'model assembly smoke gate' },
  @{ Key = 'filament_mapping'; Output = 'filament_mapping'; Patterns = '.github/print-filament-mapping-smoke-paths.txt'; Label = 'print filament mapping smoke gate' },
  @{ Key = 'apply_validate'; Output = 'apply_validate'; Patterns = '.github/print-apply-validate-smoke-paths.txt'; Label = 'print apply/validate smoke gate' },
  @{ Key = 'perimeters'; Output = 'perimeters'; Patterns = '.github/print-perimeters-smoke-paths.txt'; Label = 'print perimeters smoke gate' },
  @{ Key = 'process_math'; Output = 'process_math'; Patterns = '.github/print-process-math-smoke-paths.txt'; Label = 'print process math smoke gate' },
  @{ Key = 'process_core'; Output = 'process_core'; Patterns = '.github/print-process-core-smoke-paths.txt'; Label = 'print process core smoke gate' },
  @{ Key = 'object_layers'; Output = 'object_layers'; Patterns = '.github/print-object-layers-smoke-paths.txt'; Label = 'print object layers smoke gate' },
  @{ Key = 'adhesion'; Output = 'adhesion'; Patterns = '.github/print-adhesion-smoke-paths.txt'; Label = 'print adhesion smoke gate' },
  @{ Key = 'fill'; Output = 'fill'; Patterns = '.github/fill-smoke-paths.txt'; Label = 'fill smoke gate' },
  @{ Key = 'support_material_layers'; Output = 'support_material_layers'; Patterns = '.github/support-material-layers-smoke-paths.txt'; Label = 'support material layers smoke gate' },
  @{ Key = 'support_material'; Output = 'support_material'; Patterns = '.github/support-material-smoke-paths.txt'; Label = 'support material smoke gate' },
  @{ Key = 'trianglemesh_geometry'; Output = 'trianglemesh_geometry'; Patterns = '.github/trianglemesh-geometry-smoke-paths.txt'; Label = 'trianglemesh geometry smoke gate' }
)

$states = [ordered]@{}
foreach ($scope in $scopeDefs) {
  $states[$scope.Key] = @{
    Enabled = $true
    Reason = 'workflow_dispatch'
    Matched = 'n/a'
  }
}

function Set-AllScopes {
  param(
    [bool]$Enabled,
    [string]$Reason,
    [string]$Matched
  )

  foreach ($scope in $scopeDefs) {
    $states[$scope.Key].Enabled = $Enabled
    $states[$scope.Key].Reason = $Reason
    $states[$scope.Key].Matched = $Matched
  }
}

function Enable-Scope {
  param(
    [string]$Key,
    [string]$Reason,
    [string]$Matched
  )

  $states[$Key].Enabled = $true
  $states[$Key].Reason = $Reason
  $states[$Key].Matched = $Matched
}

function Get-ScopeResult {
  param(
    [string[]]$ChangedFiles,
    [string]$PatternsPath
  )

  $patterns = Get-Content $PatternsPath |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and -not $_.Trim().StartsWith('#') } |
    ForEach-Object { $_.Trim() }

  $matched = New-Object System.Collections.Generic.List[string]
  foreach ($file in $ChangedFiles) {
    foreach ($pattern in $patterns) {
      if ($file -like $pattern) {
        $matched.Add($file)
        break
      }
    }
  }

  $matchedUnique = @($matched | Select-Object -Unique)
  return @{
    Enabled = ($matchedUnique.Count -gt 0)
    Reason  = if ($matchedUnique.Count -gt 0) { 'matched_paths' } else { 'no_matching_paths' }
    Matched = if ($matchedUnique.Count -gt 0) { $matchedUnique -join ', ' } else { 'n/a' }
  }
}

function Test-CMakeSourceListEntry {
  param([string]$Line)

  return $Line -match '^[A-Za-z0-9_./+-]+\.(c|cc|cpp|cxx|h|hpp)$'
}

function Get-CMakeScopesFromContext {
  param([string]$Context)

  if ($Context -match 'libslic3r_config_core_sources|add_library\(libslic3r_config_core') {
    return @('config')
  }
  if ($Context -match 'libslic3r_placeholder_parser_core_sources|add_library\(libslic3r_placeholder_parser_core') {
    return @('placeholder_parser')
  }
  if ($Context -match 'libslic3r_gcodewriter_core_sources|add_library\(libslic3r_gcodewriter_core') {
    return @('gcodewriter')
  }
  if ($Context -match 'libslic3r_gcode_reader_core_sources|add_library\(libslic3r_gcode_reader_core') {
    return @('gcode_reader')
  }
  if ($Context -match 'libslic3r_model_basic_core_sources|add_library\(libslic3r_model_basic_core') {
    return @('model_assembly')
  }
  if ($Context -match 'libslic3r_print_process_math_core_sources|add_library\(libslic3r_print_process_math_core') {
    return @('process_math', 'fill')
  }
  if ($Context -match 'libslic3r_fill_core_sources|add_library\(libslic3r_fill_core') {
    return @('fill')
  }
  if ($Context -match 'libslic3r_trianglemesh_geometry_core_sources|add_library\(libslic3r_trianglemesh_geometry_core') {
    return @('trianglemesh_geometry')
  }
  if ($Context -match 'libslic3r_print_object_layers_core_sources|add_library\(libslic3r_print_object_layers_core') {
    return @('object_layers')
  }
  if ($Context -match 'libslic3r_print_process_core_sources|add_library\(libslic3r_print_process_core') {
    return @('process_core', 'adhesion', 'support_material', 'perimeters', 'apply_validate', 'filament_mapping')
  }
  if ($Context -match 'libslic3r_print_stage_base_core_sources|add_library\(libslic3r_print_stage_base_core|configure_print_smoke_core\(libslic3r_print_stage_base_core|target_link_libraries\(libslic3r_print_(process|perimeters)_core.*libslic3r_print_stage_base_core') {
    return @('process_core', 'adhesion', 'support_material', 'perimeters', 'apply_validate', 'filament_mapping')
  }
  if ($Context -match 'libslic3r_print_perimeters_core_sources|add_library\(libslic3r_print_perimeters_core') {
    return @('perimeters', 'apply_validate', 'filament_mapping')
  }
  if ($Context -match 'libslic3r_print_apply_base_core_sources|add_library\(libslic3r_print_apply_base_core|configure_print_smoke_core\(libslic3r_print_apply_base_core|target_link_libraries\(libslic3r_print_(filament_mapping|apply_validate)_core.*libslic3r_print_apply_base_core') {
    return @('apply_validate', 'filament_mapping')
  }
  if ($Context -match 'libslic3r_print_apply_validate_core_sources|add_library\(libslic3r_print_apply_validate_core') {
    return @('apply_validate', 'filament_mapping')
  }
  if ($Context -match 'libslic3r_print_filament_mapping_core_sources|add_library\(libslic3r_print_filament_mapping_core') {
    return @('filament_mapping')
  }

  return @()
}

function Get-CentralCMakeScopeResult {
  param(
    [string]$BaseRef,
    [string]$Head
  )

  $enabled = @{}
  foreach ($scope in $scopeDefs) {
    $enabled[$scope.Key] = $false
  }

  $unknown = New-Object System.Collections.Generic.List[string]
  $matched = New-Object System.Collections.Generic.List[string]
  $currentScopes = @()
  $fullSourceListOnly = $false
  $diffLines = @(git diff --unified=0 $BaseRef $Head -- src/libslic3r/CMakeLists.txt)
  if ($LASTEXITCODE -ne 0) {
    return @{
      Classified = $false
      Reason = 'cmake_diff_failed'
      Matched = 'src/libslic3r/CMakeLists.txt'
      Enabled = $enabled
    }
  }

  foreach ($line in $diffLines) {
    if ($line -match '^@@ .* @@(?<context>.*)$') {
      $currentScopes = @(Get-CMakeScopesFromContext $matches.context)
      continue
    }
    if ($line -match '^(diff --git|index |--- |\+\+\+ )') {
      continue
    }
    if (-not ($line.StartsWith('+') -or $line.StartsWith('-'))) {
      continue
    }

    $changedLine = $line.Substring(1).Trim()
    if ([string]::IsNullOrWhiteSpace($changedLine) -or $changedLine.StartsWith('#')) {
      continue
    }

    if ($currentScopes.Count -gt 0) {
      foreach ($scope in $currentScopes) {
        $enabled[$scope] = $true
      }
      $matched.Add("src/libslic3r/CMakeLists.txt:$changedLine")
      continue
    }

    $changedLineScopes = @(Get-CMakeScopesFromContext $changedLine)
    if ($changedLineScopes.Count -gt 0) {
      foreach ($scope in $changedLineScopes) {
        $enabled[$scope] = $true
      }
      $matched.Add("src/libslic3r/CMakeLists.txt:$changedLine")
      continue
    }

    if (($changedLine -match 'lisbslic3r_sources|libslic3r_sources') -or (Test-CMakeSourceListEntry $changedLine)) {
      $fullSourceListOnly = $true
      $matched.Add("src/libslic3r/CMakeLists.txt:$changedLine")
      continue
    }

    $unknown.Add($changedLine)
  }

  if ($unknown.Count -gt 0) {
    return @{
      Classified = $false
      Reason = 'central_cmake_unclassified'
      Matched = "src/libslic3r/CMakeLists.txt: $($unknown -join '; ')"
      Enabled = $enabled
    }
  }

  if ($fullSourceListOnly -and -not ($enabled.Values -contains $true)) {
    return @{
      Classified = $false
      Reason = 'central_cmake_full_source_only'
      Matched = if ($matched.Count -gt 0) { ($matched | Select-Object -Unique) -join ', ' } else { 'src/libslic3r/CMakeLists.txt' }
      Enabled = $enabled
    }
  }

  return @{
    Classified = $true
    Reason = 'central_cmake_scoped'
    Matched = if ($matched.Count -gt 0) { ($matched | Select-Object -Unique) -join ', ' } else { 'src/libslic3r/CMakeLists.txt' }
    Enabled = $enabled
  }
}

function Write-OutputValue {
  param(
    [string]$Name,
    [string]$Value
  )

  if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    Write-Host "$Name=$Value"
  } else {
    "$Name=$Value" | Out-File -Append -FilePath $OutputPath -Encoding utf8
  }
}

$changed = @()
$changedText = 'n/a'

if ($EventName -ne 'workflow_dispatch') {
  $baseRef = if ($EventName -eq 'pull_request') { $PullRequestBaseSha } else { $BeforeSha }
  $head = $HeadSha

  if ([string]::IsNullOrWhiteSpace($baseRef) -or $baseRef -match '^0+$') {
    Set-AllScopes -Enabled $true -Reason 'missing_base_sha' -Matched 'n/a'
  } else {
    if (-not $SkipFetch) {
      git fetch --no-tags --depth=1 origin $baseRef | Out-Null
    }
    if ((-not $SkipFetch) -and $LASTEXITCODE -ne 0) {
      Set-AllScopes -Enabled $true -Reason 'fetch_base_failed' -Matched 'n/a'
    } else {
      $baseDiffRef = if ($SkipFetch) { $baseRef } else { 'FETCH_HEAD' }
      $changed = @(git diff --name-only $baseDiffRef $head | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
      $changedText = if ($changed.Count -gt 0) { $changed -join ', ' } else { 'n/a' }

      foreach ($scope in $scopeDefs) {
        $scopeResult = Get-ScopeResult -ChangedFiles $changed -PatternsPath $scope.Patterns
        $states[$scope.Key].Enabled = $scopeResult.Enabled
        $states[$scope.Key].Reason = $scopeResult.Reason
        $states[$scope.Key].Matched = $scopeResult.Matched
      }

      $scriptChanged = @($changed | Where-Object { $_ -eq '.github/scripts/detect-cpp-smoke-scopes.ps1' }).Count -gt 0
      if ($scriptChanged) {
        Set-AllScopes -Enabled $true -Reason 'smoke_detector_changed' -Matched '.github/scripts/detect-cpp-smoke-scopes.ps1'
      }

      $centralCMakeChanged = @($changed | Where-Object { $_ -eq 'src/libslic3r/CMakeLists.txt' }).Count -gt 0
      if ($centralCMakeChanged) {
        $cmakeScope = Get-CentralCMakeScopeResult -BaseRef $baseDiffRef -Head $head
        if ($cmakeScope.Classified) {
          foreach ($scope in $scopeDefs) {
            if ($cmakeScope.Enabled[$scope.Key] -and -not $states[$scope.Key].Enabled) {
              Enable-Scope -Key $scope.Key -Reason $cmakeScope.Reason -Matched $cmakeScope.Matched
            }
          }
        } else {
          Set-AllScopes -Enabled $true -Reason $cmakeScope.Reason -Matched $cmakeScope.Matched
        }
      }
    }
  }
}

$anyEnabled = $false
foreach ($scope in $scopeDefs) {
  $anyEnabled = $anyEnabled -or [bool]$states[$scope.Key].Enabled
}

Write-OutputValue -Name 'any_enabled' -Value $anyEnabled.ToString().ToLower()
Write-OutputValue -Name 'changed' -Value $changedText
foreach ($scope in $scopeDefs) {
  $state = $states[$scope.Key]
  Write-OutputValue -Name "$($scope.Output)_enabled" -Value $state.Enabled.ToString().ToLower()
  Write-OutputValue -Name "$($scope.Output)_reason" -Value $state.Reason
  Write-OutputValue -Name "$($scope.Output)_matched" -Value $state.Matched
}

Write-Host "smoke any_enabled=$anyEnabled"
if ($changed.Count -gt 0) {
  Write-Host 'changed files:'
  $changed | ForEach-Object { Write-Host "  $_" }
}
foreach ($scope in $scopeDefs) {
  $state = $states[$scope.Key]
  Write-Host "$($scope.Label) enabled=$($state.Enabled) reason=$($state.Reason) matched=$($state.Matched)"
}
