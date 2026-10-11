$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskNm = 'C:\Program Files\Arm\GNU Toolchain mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-nm.exe'
$taskSymbols = & $taskNm -S (Join-Path $taskRoot 'build-gp5\chunithm_pico_bridge.elf')
$taskBinary = [IO.File]::ReadAllBytes((Join-Path $taskRoot 'build-gp5\chunithm_pico_bridge.bin'))
function Read-Symbol([string]$name) {
    $entry = $taskSymbols | Where-Object { $_ -match "\s$name$" }
    if (@($entry).Count -ne 1) { throw "Missing symbol $name" }
    $parts = $entry -split '\s+'
    $offset = [Convert]::ToInt64($parts[0],16) - 0x10000000
    $length = [Convert]::ToInt32($parts[1],16)
    return ,([byte[]]$taskBinary[$offset..($offset+$length-1)])
}
function Require([bool]$condition, [string]$message) { if (!$condition) { throw $message } }
$descriptor = Read-Symbol 'io4_report_descriptor'
$globals = @{}; $locals = @{}; $fields = [Collections.Generic.List[object]]::new(); $positions = @{}
for ($cursor = 0; $cursor -lt $descriptor.Length;) {
    $prefix = $descriptor[$cursor++]; Require ($prefix -ne 254) 'Unexpected long HID item'
    $size = $prefix -band 3; if ($size -eq 3) { $size = 4 }
    Require ($cursor+$size -le $descriptor.Length) 'Truncated HID item'
    $type = ($prefix -shr 2) -band 3; $tag = [int]($prefix -shr 4)
    [uint32]$value = 0
    for ($b = 0; $b -lt $size; $b++) { $value = $value -bor ([uint32]$descriptor[$cursor++] -shl (8*$b)) }
    if ($type -eq 1) { $globals[$tag] = $value }
    elseif ($type -eq 2) { $locals[$tag] = $value }
    elseif ($type -eq 0) {
        if ($tag -in 8,9,11) {
            $key = "$tag/$($globals[8])"
            if (!$positions.ContainsKey($key)) { $positions[$key] = 0 }
            $fields.Add([pscustomobject]@{Kind=$tag;Id=$globals[8];Page=$globals[0];Min=$globals[1];Max=$globals[2];Size=$globals[7];Count=$globals[9];Flags=$value;Offset=$positions[$key];Usage=$locals[0];UsageMin=$locals[1];UsageMax=$locals[2]})
            $positions[$key] += $globals[7]*$globals[9]
        }
        $locals.Clear()
    }
}
$inputFields = @($fields | Where-Object Kind -eq 8)
Require ($inputFields.Count -eq 16) 'Unexpected input field count'
$axes = @(48,49,48,49,48,49,48,49,51,52,51,52,54,54)
for ($i = 0; $i -lt 14; $i++) {
    $f = $inputFields[$i]
    Require ($f.Id -eq 1 -and $f.Page -eq 1 -and $f.Min -eq 0 -and $f.Max -eq 65535 -and $f.Size -eq 16 -and $f.Count -eq 1 -and $f.Flags -eq 2 -and $f.Offset -eq 16*$i -and $f.Usage -eq $axes[$i]) "Axis layout/range changed at $i"
}
$f = $inputFields[14]
Require ($f.Page -eq 9 -and $f.Min -eq 0 -and $f.Max -eq 1 -and $f.Size -eq 1 -and $f.Count -eq 48 -and $f.Offset -eq 224 -and $f.UsageMin -eq 1 -and $f.UsageMax -eq 48 -and $f.Flags -eq 2) 'Button page/range/offset invalid'
$f = $inputFields[15]
Require ($f.Offset -eq 272 -and $f.Size*$f.Count -eq 232 -and ($f.Flags -band 1) -eq 1) 'Padding changed'
$outputFields = @($fields | Where-Object Kind -eq 9)
Require ($outputFields.Count -eq 1) 'Output field count changed'
$f = $outputFields[0]
Require ($f.Id -eq 16 -and $f.Page -eq 0xffa0 -and $f.Min -eq 0 -and $f.Max -eq 255 -and $f.Count -eq 63 -and $f.Size -eq 8) 'IO4 output layout changed'
Require ($positions['8/1'] -eq 504 -and $positions['9/16'] -eq 504) 'Report length changed'
$config = Read-Symbol 'configuration_descriptor'
$control = Read-Symbol 'control_report_descriptor'
Require ($config.Length -eq 205 -and $config[4] -eq 6) 'Composite interface layout changed'
Require (($config[2]+256*$config[3]) -eq $config.Length) 'Configuration total length invalid'
$hidCount = 0; $endpoints = @()
for ($cursor = 0; $cursor -lt $config.Length; $cursor += $config[$cursor]) {
    Require ($config[$cursor] -ge 2) 'Invalid USB descriptor length'
    if ($config[$cursor+1] -eq 0x21) {
        $expected = if ($hidCount -eq 0) { $descriptor.Length } else { $control.Length }
        Require (($config[$cursor+7]+256*$config[$cursor+8]) -eq $expected) 'HID descriptor length mismatch'
        $hidCount++
    }
    if ($config[$cursor+1] -eq 5) { $endpoints += $config[$cursor+2] }
}
Require ($hidCount -eq 2) 'Expected two independent HID interfaces'
Require (@($endpoints | Select-Object -Unique).Count -eq $endpoints.Count) 'Duplicate USB endpoint address'
Require ($endpoints -contains 4 -and $endpoints -contains 134) 'Missing control endpoints'
Require (($control -join ',') -match '6,96,255,9,1,161,1,133,1') 'Vendor control collection or report ID invalid'
Require (($control -join ',') -match '117,8,149,63') 'Control report payload must be 63 bytes'
'PASS: compiled IO4 field layouts and ranges, 205-byte composite configuration, two HID interfaces, unique endpoints and vendor control report'
