# Инструкция по сборке xrRender_Vulkan

## ✅ Все готово к сборке!

Vulkan рендер полностью интегрирован в engine-vs2022.sln и готов к компиляции.

---

## Предварительные требования

### 1. Vulkan SDK (ОБЯЗАТЕЛЬНО!)

Если еще не установлен, см. **INSTALL_VULKAN_SDK.md**

**Быстрая проверка:**
```cmd
echo %VULKAN_SDK%
```
Должно показать: `C:\VulkanSDK\1.3.xxx.x`

### 2. Visual Studio 2022

- Build Tools или полная версия
- C++ Desktop Development workload
- Windows 10/11 SDK

---

## Сборка

### Вариант 1: Через Visual Studio (GUI)

1. **Открыть solution:**
   ```
   src\engine-vs2022.sln
   ```

2. **Выбрать конфигурацию:**
   - Configuration: **VerifiedDX11**
   - Platform: **x64**

3. **Собрать проект:**
   - В Solution Explorer найти `xrRender_Vulkan`
   - Правый клик → **Build**

4. **Или собрать весь solution:**
   - Build → **Build Solution** (Ctrl+Shift+B)

### Вариант 2: Через командную строку (MSBuild)

```cmd
:: Переход в директорию с solution
cd src

:: Сборка только Vulkan рендера
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" engine-vs2022.sln /t:xrRender_Vulkan /p:Configuration=VerifiedDX11 /p:Platform=x64

:: Или сборка всего solution
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" engine-vs2022.sln /p:Configuration=VerifiedDX11 /p:Platform=x64
```

### Вариант 3: Через PowerShell скрипт

Создайте `build_vulkan.ps1`:
```powershell
# build_vulkan.ps1
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "engine-vs2022.sln"
$Config = "VerifiedDX11"
$Platform = "x64"

Write-Host "Building xrRender_Vulkan..." -ForegroundColor Green

& $MSBuild $Solution /t:xrRender_Vulkan /p:Configuration=$Config /p:Platform=$Platform /v:minimal

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild succeeded!" -ForegroundColor Green
    Write-Host "Output: _build\bin_dbg\VerifiedDX11\xrRender_Vulkan.dll" -ForegroundColor Cyan
} else {
    Write-Host "`nBuild failed!" -ForegroundColor Red
}
```

Запуск:
```powershell
cd src
.\build_vulkan.ps1
```

---

## Проверка результата

### 1. DLL создана

```cmd
dir _build\bin_dbg\VerifiedDX11\xrRender_Vulkan.dll
```

Должно показать файл размером ~100-200KB (зависит от конфигурации).

### 2. Зависимости скопированы

```cmd
dir _build\bin_dbg\VerifiedDX11\*.dll
```

Должны быть:
- `xrRender_Vulkan.dll` ✓
- `xrCore.dll` (зависимость)
- Другие движковые DLL

### 3. Нет ошибок компиляции

Проверьте Output в Visual Studio или лог MSBuild:
- **0 Error(s)** ✓
- Warnings допустимы (обычно W4 уровня)

---

## Возможные ошибки и решения

### ❌ Ошибка: "VULKAN_SDK is not defined"

**Причина:** Vulkan SDK не установлен или переменная окружения не настроена.

**Решение:**
1. Установить Vulkan SDK (см. INSTALL_VULKAN_SDK.md)
2. Перезапустить Visual Studio/Terminal
3. Проверить: `echo %VULKAN_SDK%`

### ❌ Ошибка: "Cannot open include file: 'vulkan/vulkan.h'"

**Причина:** Неверный путь к Vulkan SDK.

**Решение:**
Проверить в vcxproj:
```xml
<AdditionalIncludeDirectories>...;$(VULKAN_SDK)\Include;...</AdditionalIncludeDirectories>
```

### ❌ Ошибка: "unresolved external symbol vmaCreateAllocator"

**Причина:** `vma_impl.cpp` не скомпилирован.

**Решение:**
1. Проверить что `vma_impl.cpp` в проекте
2. Clean + Rebuild проекта

### ❌ Ошибка: "C1189: Please disable exceptions"

**Причина:** VMA требует exceptions, но они отключены.

**Решение:**
Уже исправлено в vcxproj! Если ошибка все еще есть:
1. Проверить настройки `vma_impl.cpp` (должен иметь `<ExceptionHandling>Sync</ExceptionHandling>`)
2. См. EXCEPTIONS_FIX.md для деталей

### ❌ Ошибка: "Cannot open file: xrCore.lib"

**Причина:** Зависимость xrCore не собрана.

**Решение:**
```cmd
:: Собрать xrCore сначала
msbuild engine-vs2022.sln /t:xrCore /p:Configuration=VerifiedDX11 /p:Platform=x64

:: Затем Vulkan рендер
msbuild engine-vs2022.sln /t:xrRender_Vulkan /p:Configuration=VerifiedDX11 /p:Platform=x64
```

### ❌ Warning: C4530 (exceptions not enabled)

**Это нормально!** X-Ray использует exceptions выборочно. Warning можно игнорировать.

---

## Запуск игры с Vulkan рендером

После успешной сборки:

```cmd
cd _build\_game\bin

:: Запуск с Vulkan рендером
xrEngine.exe -renderer renderer_vk
```

### Ожидаемый результат:

1. **Движок запускается**
2. **В логе видно:**
   ```
   [Vulkan] SetupEnv called
   [Vulkan] Render console initialized
   [Vulkan] Checking Vulkan support...
   [Vulkan] Instance created successfully
   [Vulkan] Found NVIDIA/AMD/Intel: <GPU name>
   [Vulkan] Selected: <GPU> (score: XXX)
   ```

3. **Экран залит синим цветом** (временно, пока нет геометрии)

### Если движок не запускается:

1. **Проверить лог:** `_build\_game\logs\xray_<date>.log`
2. **Проверить DLL:** убедиться что `xrRender_Vulkan.dll` в папке bin
3. **Проверить драйверы GPU** (должны поддерживать Vulkan 1.3)

---

## Следующие шаги

После успешной компиляции:

### ✅ Фаза 1 завершена!

Базовая инфраструктура работает:
- Vulkan Instance
- Device Selection
- Swapchain
- Command Buffers
- Synchronization
- Clear screen render

### 🚀 Готовы к Фазе 2

Следующий этап: **Full Deferred Rendering Pipeline**

См. **VULKAN_IMPLEMENTATION_GUIDE.md** → Фаза 2:
- G-Buffer система
- SPIR-V шейдеры
- Pipeline management
- Vertex/Index buffers
- Текстуры
- Освещение

---

## Чистка (Clean)

### Через Visual Studio:
Build → Clean Solution

### Через командную строку:
```cmd
cd src
msbuild engine-vs2022.sln /t:Clean /p:Configuration=VerifiedDX11 /p:Platform=x64
```

### Полная чистка:
```cmd
:: Удалить все временные файлы
rmdir /s /q _build\intermediate\VerifiedDX11\xrRender_Vulkan
rmdir /s /q _build\bin_dbg\VerifiedDX11
```

---

## Конфигурации

Проект настроен для:
- **Debug** → VerifiedDX11
- **Release** → VerifiedDX11
- **VerifiedDX11** → VerifiedDX11
- **Все остальные** → VerifiedDX11

Почему VerifiedDX11?
- Есть в xrCore (нет Debug|x64)
- Включает отладочную информацию
- Подходит для разработки

---

*Документ создан: 2026-01-24*
*Версия: Фаза 1 Complete - Ready to Build*
