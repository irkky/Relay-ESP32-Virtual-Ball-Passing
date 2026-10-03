$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    python scripts/generate_players.py
    if ($LASTEXITCODE -ne 0) { throw 'Configuration generation failed' }
    g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/VirtualBall tests/game_test.cpp firmware/VirtualBall/game.cpp firmware/VirtualBall/protocol.cpp -o build/game_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed' }
    & ./build/game_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Game tests failed' }
    node --check dashboard/app.js
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard syntax check failed' }
    node tests/serial_test.js
    if ($LASTEXITCODE -ne 0) { throw 'Serial tests failed' }
} finally { Pop-Location }
