param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$Test,[switch]$Deploy)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$devRoot=Join-Path $env:USERPROFILE 'Documents\DevTools\TransWithAI'
$qtRoot=if($env:FILM_INDEX_QT){$env:FILM_INDEX_QT}else{Join-Path $devRoot 'Qt\6.8.3\mingw_64'}
$compilerRoot=if($env:FILM_INDEX_MINGW){$env:FILM_INDEX_MINGW}else{Join-Path $devRoot 'Qt\Tools\mingw1310_64'}
$cmakeBin=if($env:FILM_INDEX_CMAKE){$env:FILM_INDEX_CMAKE}else{Join-Path $devRoot 'CMake\bin'}
$env:PATH="$qtRoot\bin;$compilerRoot\bin;$cmakeBin;$env:PATH"
if(!(Test-Path "$qtRoot\bin\qmake.exe")){throw 'Qt SDK missing. Set FILM_INDEX_QT / FILM_INDEX_MINGW / FILM_INDEX_CMAKE or follow README.'}
cmake -S $projectRoot -B "$projectRoot\build" -G Ninja "-DCMAKE_PREFIX_PATH=$qtRoot" "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$compilerRoot/bin/g++.exe" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if($LASTEXITCODE){throw 'CMake configure failed'}
cmake --build "$projectRoot\build" --parallel 6
if($LASTEXITCODE){throw 'Build failed'}
if($Test){ctest --test-dir "$projectRoot\build" --output-on-failure;if($LASTEXITCODE){throw 'Tests failed'}}
if($Deploy){
    $dist=Join-Path $projectRoot 'dist\Film-Index-Studio-1.0.4'
    New-Item -ItemType Directory -Force $dist | Out-Null
    Copy-Item "$projectRoot\build\FilmIndexStudio.exe" $dist
    & "$qtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$dist\FilmIndexStudio.exe"
    if($LASTEXITCODE){throw 'Qt deployment failed'}
    Copy-Item "$projectRoot\README.md","$projectRoot\USER_GUIDE.md","$projectRoot\LICENSE","$projectRoot\THIRD_PARTY_NOTICES.md","$projectRoot\RELEASE_NOTES.md" $dist
    Copy-Item "$projectRoot\licenses" $dist -Recurse -Force
    Copy-Item "$projectRoot\scripts\start-portable.bat" "$dist\FilmIndexStudio.bat"
    New-Item -ItemType Directory -Force "$dist\templates","$dist\presets" | Out-Null
    Get-ChildItem -LiteralPath "$projectRoot\assets\film_strips\135" -Directory | ForEach-Object {
        $target=Join-Path "$dist\templates" $_.Name
        if(!(Test-Path -LiteralPath $target)){Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse}
    }
    Copy-Item -LiteralPath "$projectRoot\assets\film_strips\README.md" -Destination "$dist\templates\README.md"
}
