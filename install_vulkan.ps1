# Установка Vulkan SDK
$VulkanVersion = "1.3.296.0"
$DownloadUrl = "https://sdk.lunarg.com/sdk/download/$VulkanVersion/windows/VulkanSDK-$VulkanVersion-Installer.exe"
$InstallerPath = "$env:TEMP\VulkanSDK-Installer.exe"

Write-Host "Downloading Vulkan SDK $VulkanVersion..." -ForegroundColor Green
Write-Host "URL: $DownloadUrl" -ForegroundColor Cyan

try {
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $InstallerPath -ErrorAction Stop
    Write-Host "Downloaded successfully to: $InstallerPath" -ForegroundColor Green

    $fileInfo = Get-Item $InstallerPath
    Write-Host "File size: $($fileInfo.Length / 1MB) MB" -ForegroundColor Cyan

    Write-Host "`nStarting installation..." -ForegroundColor Green
    Write-Host "This will install Vulkan SDK silently. Please wait..." -ForegroundColor Yellow

    Start-Process -FilePath $InstallerPath -ArgumentList "/S" -Wait -NoNewWindow

    Write-Host "`nVulkan SDK installation completed!" -ForegroundColor Green
    Write-Host "IMPORTANT: You must restart your terminal/IDE to apply environment variables!" -ForegroundColor Yellow

    # Попытка найти установленную версию
    if (Test-Path "C:\VulkanSDK") {
        Write-Host "`nInstalled versions:" -ForegroundColor Cyan
        Get-ChildItem "C:\VulkanSDK" -Directory | Select-Object Name
    }

} catch {
    Write-Host "`nError during download/installation:" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "`nPlease install manually from: https://vulkan.lunarg.com/" -ForegroundColor Yellow
}
