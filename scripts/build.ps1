param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$Test,[switch]$Deploy)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$devRoot=Join-Path $env:USERPROFILE 'Documents\DevTools\TransWithAI'
$qtRoot=if($env:FILMLOG_QT){$env:FILMLOG_QT}else{Join-Path $devRoot 'Qt\6.8.3\mingw_64'}
$compilerRoot=if($env:FILMLOG_MINGW){$env:FILMLOG_MINGW}else{Join-Path $devRoot 'Qt\Tools\mingw1310_64'}
$cmakeBin=if($env:FILMLOG_CMAKE){$env:FILMLOG_CMAKE}else{Join-Path $devRoot 'CMake\bin'}
$env:PATH="$qtRoot\bin;$compilerRoot\bin;$cmakeBin;$env:PATH"
if(!(Test-Path "$qtRoot\bin\qmake.exe")){throw 'Qt SDK missing. Set FILMLOG_QT / FILMLOG_MINGW / FILMLOG_CMAKE or follow README.'}
cmake -S $projectRoot -B "$projectRoot\build" -G Ninja "-DCMAKE_PREFIX_PATH=$qtRoot" "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$compilerRoot/bin/g++.exe" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if($LASTEXITCODE){throw 'CMake configure failed'}
cmake --build "$projectRoot\build" --parallel 6
if($LASTEXITCODE){throw 'Build failed'}
if($Test){ctest --test-dir "$projectRoot\build" --output-on-failure;if($LASTEXITCODE){throw 'Tests failed'}}
if($Deploy){
    $dist=Join-Path $projectRoot 'dist'
    New-Item -ItemType Directory -Force $dist | Out-Null
    Copy-Item "$projectRoot\build\FilmLogIndexStudio.exe" $dist
    & "$qtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$dist\FilmLogIndexStudio.exe"
    if($LASTEXITCODE){throw 'Qt deployment failed'}
    Copy-Item "$projectRoot\README.md","$projectRoot\LICENSE","$projectRoot\THIRD_PARTY_NOTICES.md" $dist
    Copy-Item "$projectRoot\licenses" $dist -Recurse -Force
}
