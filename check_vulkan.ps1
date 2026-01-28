# Проверка установки Vulkan SDK
Write-Host "=== Проверка Vulkan SDK ===" -ForegroundColor Cyan

# Проверка переменной окружения (Machine)
$sdkMachine = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine')
if ($sdkMachine) {
    Write-Host "VULKAN_SDK (Machine): $sdkMachine" -ForegroundColor Green
} else {
    Write-Host "VULKAN_SDK (Machine): не установлена" -ForegroundColor Yellow
}

# Проверка переменной окружения (User)
$sdkUser = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'User')
if ($sdkUser) {
    Write-Host "VULKAN_SDK (User): $sdkUser" -ForegroundColor Green
} else {
    Write-Host "VULKAN_SDK (User): не установлена" -ForegroundColor Yellow
}

# Проверка текущей сессии
if ($env:VULKAN_SDK) {
    Write-Host "VULKAN_SDK (Current): $env:VULKAN_SDK" -ForegroundColor Green
} else {
    Write-Host "VULKAN_SDK (Current): не установлена (требуется перезапуск terminal)" -ForegroundColor Yellow
}

# Поиск директории VulkanSDK
Write-Host "`n=== Поиск установленных версий ===" -ForegroundColor Cyan
if (Test-Path "C:\VulkanSDK") {
    $versions = Get-ChildItem "C:\VulkanSDK" -Directory
    if ($versions) {
        foreach ($ver in $versions) {
            Write-Host "Найдена версия: $($ver.Name)" -ForegroundColor Green
            $headerPath = Join-Path $ver.FullName "Include\vulkan\vulkan.h"
            $libPath = Join-Path $ver.FullName "Lib\vulkan-1.lib"

            if (Test-Path $headerPath) {
                Write-Host "  ✓ Headers найдены" -ForegroundColor Green
            } else {
                Write-Host "  ✗ Headers НЕ найдены" -ForegroundColor Red
            }

            if (Test-Path $libPath) {
                Write-Host "  ✓ Libraries найдены" -ForegroundColor Green
            } else {
                Write-Host "  ✗ Libraries НЕ найдены" -ForegroundColor Red
            }
        }
    } else {
        Write-Host "C:\VulkanSDK существует, но пуста" -ForegroundColor Yellow
    }
} else {
    Write-Host "C:\VulkanSDK не найдена" -ForegroundColor Red
}

# Проверка Vulkan runtime
Write-Host "`n=== Vulkan Runtime ===" -ForegroundColor Cyan
$vulkaninfo = Get-Command vulkaninfo -ErrorAction SilentlyContinue
if ($vulkaninfo) {
    Write-Host "vulkaninfo.exe: $($vulkaninfo.Source)" -ForegroundColor Green
} else {
    Write-Host "vulkaninfo.exe не найден" -ForegroundColor Red
}

Write-Host "`n=== Рекомендации ===" -ForegroundColor Cyan
if (-not $sdkMachine -and -not $sdkUser) {
    Write-Host "SDK не установлен. Варианты:" -ForegroundColor Yellow
    Write-Host "1. Запустите установщик вручную: C:\Users\egorb\AppData\Local\Temp\VulkanSDK-Installer.exe" -ForegroundColor Yellow
    Write-Host "2. Или скачайте с https://vulkan.lunarg.com/" -ForegroundColor Yellow
} else {
    Write-Host "SDK установлен! Перезапустите terminal чтобы применить переменные окружения." -ForegroundColor Green
}
