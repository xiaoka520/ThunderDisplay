# Read-only diagnostics for the active ThunderDisplay connection.
$ErrorActionPreference = 'Stop'
$report = New-Object System.Text.StringBuilder
function Add-Section($name, $read) {
    [void]$report.AppendLine("`r`n--- $name ---")
    try { [void]$report.AppendLine((& $read | Out-String -Width 220)) }
    catch { [void]$report.AppendLine('Unavailable: ' + $_.Exception.Message) }
}
[void]$report.AppendLine('ThunderDisplay link report ' + (Get-Date -Format o))
Add-Section 'Windows' { Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber }
Add-Section 'TCP settings' { netsh interface tcp show global; netsh interface tcp show heuristics }
$clients = @(Get-Process ThunderDisplayClient -ErrorAction SilentlyContinue)
$connections = @(Get-NetTCPConnection -State Established -ErrorAction SilentlyContinue | Where-Object { $_.OwningProcess -in $clients.Id })
Add-Section 'TD connections' { $connections | Select-Object LocalAddress,LocalPort,RemoteAddress,RemotePort,OwningProcess }
$addresses = @($connections.LocalAddress | Sort-Object -Unique)
$indices = @(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -in $addresses } | Select-Object -ExpandProperty InterfaceIndex -Unique)
$adapters = @(Get-NetAdapter -IncludeHidden | Where-Object { $_.InterfaceIndex -in $indices })
if (!$adapters.Count) { [void]$report.AppendLine('No active TD adapter found. Keep TD connected and rerun this check.') }
foreach ($adapter in $adapters) {
    Add-Section 'TD adapter and driver' { $adapter | Format-List Name,InterfaceDescription,Status,LinkSpeed,ReceiveLinkSpeed,TransmitLinkSpeed,DriverInformation,DriverFileName,DriverVersion,DriverDate,PnPDeviceID }
    Add-Section 'TD interface' { Get-NetIPInterface -InterfaceIndex $adapter.InterfaceIndex | Format-List AddressFamily,NlMtu,ConnectionState,InterfaceMetric }
    Add-Section 'Adapter properties' { $adapter | Get-NetAdapterAdvancedProperty -AllProperties | Format-Table DisplayName,DisplayValue,RegistryKeyword,RegistryValue -AutoSize }
    Add-Section 'Receive scaling' { $adapter | Get-NetAdapterRss | Format-List * }
    Add-Section 'Receive coalescing' { $adapter | Get-NetAdapterRsc | Format-List * }
    Add-Section 'Segmentation offload' { $adapter | Get-NetAdapterLso | Format-List * }
    Add-Section 'Adapter statistics' { $adapter | Get-NetAdapterStatistics | Format-List * }
    Add-Section 'Measured receive rate over two seconds' {
        $before = $adapter | Get-NetAdapterStatistics
        $clock = [Diagnostics.Stopwatch]::StartNew()
        Start-Sleep -Seconds 2
        $after = $adapter | Get-NetAdapterStatistics
        $clock.Stop()
        [PSCustomObject]@{Seconds=$clock.Elapsed.TotalSeconds;ReceiveGbps=($after.ReceivedBytes-$before.ReceivedBytes)*8/$clock.Elapsed.TotalSeconds/1e9;ReceivedDiscardsDelta=$after.ReceivedDiscardedPackets-$before.ReceivedDiscardedPackets;ReceivedErrorsDelta=$after.ReceivedPacketErrors-$before.ReceivedPacketErrors}
    }
}
Add-Section 'CPU per logical processor' { Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor | Select-Object Name,PercentProcessorTime,PercentPrivilegedTime,PercentInterruptTime,PercentDPCTime | Format-Table -AutoSize }
Add-Section 'Recent TD performance only' {
    $log = Join-Path $env:LOCALAPPDATA 'ThunderDisplay\Logs\client.log'
    Get-Content -LiteralPath $log -Tail 1200 | Where-Object { $_ -match ' (client\.start|video\.raw|video\.raw\.receive\.error|display\.latency|display\.frame\.gap) ' } | Select-Object -Last 100
}
$desktop = [Environment]::GetFolderPath('Desktop')
if (!$desktop) { $desktop = $env:TEMP }
$file = Join-Path $desktop ('ThunderDisplay-Link-Report-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
[IO.File]::WriteAllText($file,$report.ToString(),(New-Object System.Text.UTF8Encoding($true)))
Write-Host ('Report saved: ' + $file)
Start-Process notepad.exe -ArgumentList ('"' + $file + '"')
