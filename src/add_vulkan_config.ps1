# PowerShell script to add Vulkan configuration to X-Ray Monolith solution
# This script modifies .sln and .vcxproj files to add independent Vulkan build configuration

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "=== Adding Vulkan Configuration to X-Ray Monolith ===" -ForegroundColor Cyan

# Step 1: Add Vulkan|x64 to solution configurations
$slnPath = Join-Path $ScriptDir "engine-vs2022.sln"
Write-Host "Modifying solution file: $slnPath" -ForegroundColor Yellow

$slnContent = Get-Content $slnPath -Raw

# Add Vulkan|x64 to SolutionConfigurationPlatforms if not exists
if ($slnContent -notmatch "Vulkan\|x64 = Vulkan\|x64") {
    # Find the line before EndGlobalSection of SolutionConfigurationPlatforms
    $slnContent = $slnContent -replace "(VerifiedDX11\|x64 = VerifiedDX11\|x64)", "`$1`r`n`t`tVulkan|x64 = Vulkan|x64"
    Write-Host "  Added Vulkan|x64 to solution configurations" -ForegroundColor Green
}

# Add project configuration mappings for Vulkan
# xrEngine GUID: {2578C6D8-660D-48AE-9322-7422F8664F06}
$xrEngineGuid = "2578C6D8-660D-48AE-9322-7422F8664F06"
if ($slnContent -notmatch "\{$xrEngineGuid\}\.Vulkan") {
    $insertAfter = "{$xrEngineGuid}.VerifiedDX11|x64.Deploy.0 = VerifiedDX11|x64"
    $newLines = @"

		{$xrEngineGuid}.Vulkan|x64.ActiveCfg = ReleaseVulkan|x64
		{$xrEngineGuid}.Vulkan|x64.Build.0 = ReleaseVulkan|x64
		{$xrEngineGuid}.Vulkan|x64.Deploy.0 = ReleaseVulkan|x64
"@
    $slnContent = $slnContent -replace [regex]::Escape($insertAfter), "$insertAfter$newLines"
    Write-Host "  Added xrEngine Vulkan configuration mapping" -ForegroundColor Green
}

# xrRender_Vulkan GUID: {9665A224-850E-4740-9886-918960857777}
$xrRenderVulkanGuid = "9665A224-850E-4740-9886-918960857777"
if ($slnContent -notmatch "\{$xrRenderVulkanGuid\}\.Vulkan") {
    # Find the last xrRender_Vulkan config and add after it
    $pattern = "\{$xrRenderVulkanGuid\}\.VerifiedDX11\|x64\.(Build|Deploy)\.0 = VerifiedDX11\|x64"
    if ($slnContent -match $pattern) {
        $insertAfter = "{$xrRenderVulkanGuid}.VerifiedDX11|x64.Build.0 = VerifiedDX11|x64"
        $newLines = @"

		{$xrRenderVulkanGuid}.Vulkan|x64.ActiveCfg = Vulkan|x64
		{$xrRenderVulkanGuid}.Vulkan|x64.Build.0 = Vulkan|x64
"@
        $slnContent = $slnContent -replace [regex]::Escape($insertAfter), "$insertAfter$newLines"
        Write-Host "  Added xrRender_Vulkan configuration mapping" -ForegroundColor Green
    }
}

# For other projects, map Vulkan|x64 to Release|x64 (they don't need special Vulkan config)
$otherProjectGuids = @(
    "1BF75FEB-87DD-486C-880B-227987D191C2",  # ode
    "A19B1DF2-82EC-4364-8BDF-85D13A1C89B5",  # xrCDB
    "A0F7D1FB-59A7-4717-A7E4-96F37E91998E",  # xrCore
    "CA0649DD-D089-423A-981C-46B57A884EB9",  # xrCPU_Pipe
    "200652A6-043E-4634-8837-87983B3BD5E0",  # xrGame
    "435BAC9A-B225-457D-AB40-C9BD0CC8838C",  # xrNetServer
    "94A1C366-3D19-48E6-8170-4ADC2E70DF97",  # xrParticles
    "CCCA7859-EB86-493E-9B53-C4235F45B3C5",  # xrSound
    "94A1C366-3D19-48E6-8170-4ADC2E70DF98",  # xrXMLParser
    "1DAEC516-E52C-4A3C-A4DA-AE3553E6E0F8",  # xrAPI
    "98D24A3D-7666-4C11-9D6E-B10393CE8CBA",  # xrPhysics
    "FA169092-EA3E-40C1-8E5A-A2B575700FE8",  # crypto
    "880CD250-BA77-4DAF-A8D4-552F12DD3AE4",  # CxImage
    "44F716E7-05BD-4390-AB02-5F7DAF7FEFDE",  # lua_extensions
    "938C5808-85A1-4B5A-8CB4-D2D9D7851CB8",  # libjpeg
    "893887A1-3ABF-40A0-B931-CD0867010785",  # libogg_static
    "0BDF3377-F84E-4B42-A605-B45AAA244CB6",  # libtheora_static
    "3A214E06-B95E-4D61-A291-1F8DF2EC10FD",  # libvorbis_static
    "CEBDE98B-A6AA-46E6-BC79-FAAF823DB9EC",  # libvorbisfile_static
    "632AEEB6-DC06-4E15-9551-B2B09A4B73C5",  # LuaJIT-2
    "772FE450-F4F3-4687-A2A7-5624825089D7",  # DXERR
    "0EB257DC-5CFC-44B0-82C9-CE6B158BE473",  # NVTT
    "9646DBF8-1A5E-41F0-9328-F65910706666",  # ReShadeCompat
    "D0843040-7706-4FBF-A931-582748BAFBB1",  # imgui
    "4DDC8F7A-3B57-4250-9404-D2EE1991BE78",  # optick
    "10D13331-3282-43F9-BEA0-150D80388B52",  # OpenAL32
    "8FDD9DAC-8E9F-677E-C93A-81986A581F89"   # luabind
)

foreach ($guid in $otherProjectGuids) {
    if ($slnContent -notmatch "\{$guid\}\.Vulkan") {
        # Find last config for this project
        $pattern = "\{$guid\}\.VerifiedDX11\|x64\.(ActiveCfg|Build\.0) = "
        if ($slnContent -match "\{$guid\}\.VerifiedDX11\|x64\.Build\.0") {
            $insertAfter = $Matches[0] -replace "\.Build\.0", ".Build.0 = VerifiedDX11|x64"
            if ($slnContent -match [regex]::Escape("{$guid}.VerifiedDX11|x64.Build.0 = VerifiedDX11|x64")) {
                $newLines = @"

		{$guid}.Vulkan|x64.ActiveCfg = Release|x64
		{$guid}.Vulkan|x64.Build.0 = Release|x64
"@
                $slnContent = $slnContent -replace [regex]::Escape("{$guid}.VerifiedDX11|x64.Build.0 = VerifiedDX11|x64"), "{$guid}.VerifiedDX11|x64.Build.0 = VerifiedDX11|x64$newLines"
            }
        } elseif ($slnContent -match "\{$guid\}\.Release\|x64\.Build\.0 = Release\|x64") {
            $newLines = @"

		{$guid}.Vulkan|x64.ActiveCfg = Release|x64
		{$guid}.Vulkan|x64.Build.0 = Release|x64
"@
            $slnContent = $slnContent -replace [regex]::Escape("{$guid}.Release|x64.Build.0 = Release|x64"), "{$guid}.Release|x64.Build.0 = Release|x64$newLines"
        }
    }
}

# Save modified solution
$slnContent | Set-Content $slnPath -NoNewline
Write-Host "Solution file updated successfully" -ForegroundColor Green

Write-Host ""
Write-Host "=== Script completed ===" -ForegroundColor Cyan
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Add ReleaseVulkan|x64 configuration to xrEngine.vcxproj" -ForegroundColor White
Write-Host "2. Add Vulkan|x64 configuration to xrRender_Vulkan.vcxproj" -ForegroundColor White
Write-Host "3. Remove STATIC_RENDERER_VULKAN from DX configurations" -ForegroundColor White
