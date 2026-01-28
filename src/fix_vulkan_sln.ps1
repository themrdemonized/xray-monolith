# PowerShell script to fix Vulkan configuration mappings in solution file
$ErrorActionPreference = "Stop"

$slnPath = Join-Path $PSScriptRoot "engine-vs2022.sln"
Write-Host "Fixing Vulkan mappings in: $slnPath" -ForegroundColor Cyan

# Read file as lines for easier manipulation
$lines = Get-Content $slnPath

# Find and fix the corrupted HideSolutionNode line
$newLines = @()
$vulkanMappings = @()
$inSolutionProperties = $false
$fixedHideSolutionNode = $false

foreach ($line in $lines) {
    # Check if we're entering SolutionProperties section
    if ($line -match "GlobalSection\(SolutionProperties\)") {
        $inSolutionProperties = $true
    }

    # Check if we're leaving SolutionProperties section
    if ($inSolutionProperties -and $line -match "^\s*EndGlobalSection") {
        $inSolutionProperties = $false
    }

    # Fix the corrupted HideSolutionNode line
    if ($line -match "HideSolutionNode = FALSE\s+\{") {
        $newLines += "`t`tHideSolutionNode = FALSE"
        $fixedHideSolutionNode = $true
        # Extract the Vulkan mapping that was incorrectly appended
        if ($line -match "\{[0-9A-F-]+\}\.Vulkan\|x64\.") {
            $mapping = $line -replace ".*?(\{[0-9A-F-]+\}\.Vulkan\|x64\..*)", '$1'
            $vulkanMappings += "`t`t$mapping"
        }
        continue
    }

    # Collect Vulkan mappings that are in wrong section
    if ($inSolutionProperties -and $line -match "\{[0-9A-F-]+\}\.Vulkan\|x64\.") {
        $vulkanMappings += $line
        continue
    }

    $newLines += $line
}

if (-not $fixedHideSolutionNode) {
    Write-Host "HideSolutionNode line was not corrupted, checking for misplaced Vulkan mappings..." -ForegroundColor Yellow
}

Write-Host "Found $($vulkanMappings.Count) misplaced Vulkan mappings" -ForegroundColor Yellow

# Now insert the Vulkan mappings in the correct location (before EndGlobalSection of ProjectConfigurationPlatforms)
$finalLines = @()
$insertedMappings = $false

for ($i = 0; $i -lt $newLines.Count; $i++) {
    $line = $newLines[$i]

    # Look for the end of ProjectConfigurationPlatforms section
    # It's the first EndGlobalSection after the VerifiedDX11 entries
    if (-not $insertedMappings -and $line -match "^\s*EndGlobalSection" -and $i -gt 0) {
        $prevLine = $newLines[$i-1]
        if ($prevLine -match "\.VerifiedDX11\|x64\." -or $prevLine -match "\.Verified\|x64\.") {
            # Insert Vulkan mappings here
            foreach ($mapping in $vulkanMappings) {
                $finalLines += $mapping
            }
            $insertedMappings = $true
            Write-Host "Inserted Vulkan mappings before ProjectConfigurationPlatforms EndGlobalSection" -ForegroundColor Green
        }
    }

    $finalLines += $line
}

# Save the file
$finalLines | Out-File $slnPath -Encoding UTF8

Write-Host "Solution file fixed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "You can now build with: msbuild /p:Configuration=Vulkan /p:Platform=x64" -ForegroundColor Cyan
