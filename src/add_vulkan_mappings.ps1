# PowerShell script to add Vulkan|x64 mappings to solution file
$ErrorActionPreference = "Stop"

$slnPath = Join-Path $PSScriptRoot "engine-vs2022.sln"
Write-Host "Adding Vulkan mappings to: $slnPath" -ForegroundColor Cyan

$content = Get-Content $slnPath -Raw

# Project GUIDs and their Vulkan configuration mappings
$mappings = @{
    # xrEngine -> ReleaseVulkan|x64
    "2578C6D8-660D-48AE-9322-7422F8664F06" = "ReleaseVulkan|x64"
    # xrRender_Vulkan -> Vulkan|x64
    "9665A224-850E-4740-9886-918960857777" = "Vulkan|x64"
    # All other projects -> Release|x64
    "1BF75FEB-87DD-486C-880B-227987D191C2" = "Release|x64"  # ode
    "A19B1DF2-82EC-4364-8BDF-85D13A1C89B5" = "Release|x64"  # xrCDB
    "A0F7D1FB-59A7-4717-A7E4-96F37E91998E" = "Release|x64"  # xrCore
    "CA0649DD-D089-423A-981C-46B57A884EB9" = "Release|x64"  # xrCPU_Pipe
    "200652A6-043E-4634-8837-87983B3BD5E0" = "Release|x64"  # xrGame
    "435BAC9A-B225-457D-AB40-C9BD0CC8838C" = "Release|x64"  # xrNetServer
    "94A1C366-3D19-48E6-8170-4ADC2E70DF97" = "Release|x64"  # xrParticles
    "CCCA7859-EB86-493E-9B53-C4235F45B3C5" = "Release|x64"  # xrSound
    "94A1C366-3D19-48E6-8170-4ADC2E70DF98" = "Release|x64"  # xrXMLParser
    "1DAEC516-E52C-4A3C-A4DA-AE3553E6E0F8" = "Release|x64"  # xrAPI
    "98D24A3D-7666-4C11-9D6E-B10393CE8CBA" = "Release|x64"  # xrPhysics
    "FA169092-EA3E-40C1-8E5A-A2B575700FE8" = "Release|x64"  # crypto
    "880CD250-BA77-4DAF-A8D4-552F12DD3AE4" = "Release|x64"  # CxImage
    "44F716E7-05BD-4390-AB02-5F7DAF7FEFDE" = "Release|x64"  # lua_extensions
    "938C5808-85A1-4B5A-8CB4-D2D9D7851CB8" = "Release|x64"  # libjpeg
    "893887A1-3ABF-40A0-B931-CD0867010785" = "Release|x64"  # libogg_static
    "0BDF3377-F84E-4B42-A605-B45AAA244CB6" = "Release|x64"  # libtheora_static
    "3A214E06-B95E-4D61-A291-1F8DF2EC10FD" = "Release|x64"  # libvorbis_static
    "CEBDE98B-A6AA-46E6-BC79-FAAF823DB9EC" = "Release|x64"  # libvorbisfile_static
    "632AEEB6-DC06-4E15-9551-B2B09A4B73C5" = "Release|x64"  # LuaJIT-2
    "772FE450-F4F3-4687-A2A7-5624825089D7" = "Release|x64"  # DXERR
    "0EB257DC-5CFC-44B0-82C9-CE6B158BE473" = "Release|x64"  # NVTT
    "9646DBF8-1A5E-41F0-9328-F65910706666" = "Release|x64"  # ReShadeCompat
    "D0843040-7706-4FBF-A931-582748BAFBB1" = "Release|x64"  # imgui
    "4DDC8F7A-3B57-4250-9404-D2EE1991BE78" = "Release|x64"  # optick
    "10D13331-3282-43F9-BEA0-150D80388B52" = "Release|x64"  # OpenAL32
    "8FDD9DAC-8E9F-677E-C93A-81986A581F89" = "Release|x64"  # luabind
}

# Build the new mappings text
$newMappings = ""
foreach ($guid in $mappings.Keys) {
    $config = $mappings[$guid]
    $newMappings += "`t`t{$guid}.Vulkan|x64.ActiveCfg = $config`r`n"
    $newMappings += "`t`t{$guid}.Vulkan|x64.Build.0 = $config`r`n"
}

# Find the position to insert (before EndGlobalSection of ProjectConfigurationPlatforms)
# Look for the last VerifiedDX11 entry of the last project and insert after it
$pattern = "(\{8FDD9DAC-8E9F-677E-C93A-81986A581F89\}\.VerifiedDX11\|x64\.Build\.0 = VerifiedDX11\|x64)"
if ($content -match $pattern) {
    $content = $content -replace $pattern, "`$1`r`n$newMappings"
    Write-Host "Added Vulkan mappings for all projects" -ForegroundColor Green
} else {
    Write-Host "Could not find insertion point. Trying alternative..." -ForegroundColor Yellow
    # Alternative: insert before the NestedProjects section
    $pattern2 = "(\s+EndGlobalSection\r?\n\s+GlobalSection\(NestedProjects\))"
    if ($content -match $pattern2) {
        $content = $content -replace $pattern2, "$newMappings`$1"
        Write-Host "Added Vulkan mappings using alternative method" -ForegroundColor Green
    } else {
        Write-Host "ERROR: Could not find insertion point" -ForegroundColor Red
        exit 1
    }
}

# Save the file
$content | Set-Content $slnPath -NoNewline
Write-Host "Solution file updated successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "You can now build with: msbuild /p:Configuration=Vulkan /p:Platform=x64" -ForegroundColor Cyan
