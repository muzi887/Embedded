<#
    SBF-20 command line tool for bringing up the qibiji_init upper/lower link.

    The file is intentionally ASCII only: Windows PowerShell 5.1 reads .ps1 as
    ANSI, so any non ASCII character would be mangled.

    Examples:
      powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action probe
      powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action read
      powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action at -AtCommand "AT+CSQ?"
      powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action config4g
      powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action monitor -TimeoutMs 3000
#>
[CmdletBinding()]
param(
    [string]$PortName = 'COM10',
    [int]$BaudRate = 115200,
    [ValidateSet('probe', 'read', 'readall', 'write', 'at', 'at3', 'bleaddr', 'config4g', 'readmqtt', 'status', 'monitor', 'paramtest', 'loopback')]
    [string]$Action = 'probe',
    [string]$AtCommand = 'AT+CSQ?',
    [string]$ProductKey,
    [string]$DeviceKey,
    [string]$DeviceSecret,
    [string]$MqttAddr,
    [string]$MotorWarningCurrUpper,
    [string]$GateMaxheight,
    [string]$GateMaxturns,
    [string]$GateKaidu,
    [string]$GateKaiduCount,
    [string]$OtaFlag,
    [string]$OtaVer,
    [string]$UpdateFlag,
    # reconnect policy; 0 = not sent (the module keeps its current value),
    # otherwise 1..65535 (the module rejects 0 with +CME ERROR:53)
    [int]$SockRstim = 0,
    [int]$SockRsnum = 0,
    [int]$TimeoutMs = 3000,
    [int]$ConfigTimeoutSec = 120,
    [switch]$TraceFrames,
    [switch]$TraceRaw,
    # Keep both off by default: on boards with a DTR/RTS auto-download circuit
    # asserting them can hold the MCU in reset.
    [switch]$Dtr,
    [switch]$Rts,
    [switch]$SweepBaud
)

$ErrorActionPreference = 'Stop'

function Get-Crc8Maxim([byte[]]$Data)
{
    $crc = 0
    foreach ($b in $Data)
    {
        $crc = $crc -bxor $b
        for ($i = 0; $i -lt 8; $i++)
        {
            if ($crc -band 1) { $crc = (($crc -shr 1) -bxor 0x8C) -band 0xFF }
            else { $crc = ($crc -shr 1) -band 0xFF }
        }
    }
    return [byte]$crc
}

function New-Sbf20Frame([int]$Index, [bool]$More, [byte[]]$Payload)
{
    if ($null -eq $Payload) { $Payload = @() }
    if ($Payload.Count -gt 15) { throw "payload too long: $($Payload.Count)" }

    $flagByte = if ($More) { 0x80 } else { 0x00 }
    $flags = [byte]($flagByte -bor ($Index -band 0x7F))

    $data = New-Object System.Collections.Generic.List[byte]
    $data.Add($flags)
    foreach ($p in $Payload) { $data.Add($p) }

    $frame = New-Object System.Collections.Generic.List[byte]
    $frame.Add([byte]0x55)
    $frame.Add([byte]($data.Count + 1))
    foreach ($d in $data) { $frame.Add($d) }
    $frame.Add((Get-Crc8Maxim $frame.ToArray()))
    return , $frame.ToArray()
}

function Format-Hex([byte[]]$Data)
{
    if ($null -eq $Data -or $Data.Count -eq 0) { return '(empty)' }
    return (($Data | ForEach-Object { $_.ToString('X2') }) -join ' ')
}

function Format-Bytes([byte[]]$Data)
{
    if ($null -eq $Data -or $Data.Count -eq 0) { return '' }
    $sb = New-Object System.Text.StringBuilder
    foreach ($b in $Data)
    {
        if ($b -ge 0x20 -and $b -lt 0x7F) { [void]$sb.Append([char]$b) }
        else { [void]$sb.Append(('\x{0:X2}' -f $b)) }
    }
    return $sb.ToString()
}

# Pull one complete frame with the expected INDEX out of the byte buffer.
# Garbage (debug printf text) is skipped by resynchronising on SOF + LEN + CRC8.
function Get-Sbf20Frame($Buffer, [int]$ExpectIndex)
{
    while ($true)
    {
        if ($Buffer.Count -lt 4) { return $null }

        $start = -1
        for ($i = 0; $i -lt $Buffer.Count; $i++)
        {
            if ($Buffer[$i] -eq 0x55) { $start = $i; break }
        }
        if ($start -lt 0) { $Buffer.Clear(); return $null }
        if ($start -gt 0) { $Buffer.RemoveRange(0, $start) }
        if ($Buffer.Count -lt 2) { return $null }

        $len = $Buffer[1]
        if ($len -lt 2 -or $len -gt 17)
        {
            $Buffer.RemoveAt(0)
            continue
        }

        $frameLen = $len + 2
        if ($Buffer.Count -lt $frameLen) { return $null }

        $frame = $Buffer.GetRange(0, $frameLen).ToArray()
        $Buffer.RemoveRange(0, $frameLen)

        if ((Get-Crc8Maxim $frame) -ne 0) { continue }

        $flags = $frame[2]
        $index = $flags -band 0x7F
        $more = ($flags -band 0x80) -ne 0
        # DATA is frame[2..len+1] (len-1 bytes): FLAGS + payload(len-2), CRC is
        # at frame[len+2]. An empty payload (len==2) must stay empty: the range
        # operator would run backwards if written as 3..2.
        $payload = @()
        if ($len -gt 2) { $payload = $frame[3..$len] }

        if ($index -ne $ExpectIndex) { continue }

        return [pscustomobject]@{ Index = $index; More = $more; Payload = [byte[]]$payload }
    }
}

function Invoke-Sbf20Session($Port, [string]$Json, [int]$PairTimeoutMs, [switch]$Show)
{
    $request = [System.Text.Encoding]::UTF8.GetBytes($Json)
    $buffer = New-Object System.Collections.Generic.List[byte]
    $response = New-Object System.Collections.Generic.List[byte]
    $offset = 0
    $index = 0

    while ($true)
    {
        $payload = @()
        $more = $false
        if ($offset -lt $request.Count)
        {
            $chunk = [Math]::Min(15, $request.Count - $offset)
            $payload = $request[$offset..($offset + $chunk - 1)]
            $offset += $chunk
            $more = ($offset -lt $request.Count)
        }

        $frame = New-Sbf20Frame $index $more $payload
        if ($TraceFrames) { Write-Host ("  TX " + (Format-Hex $frame)) -ForegroundColor DarkGray }
        $Port.Write($frame, 0, $frame.Count)

        $deadline = (Get-Date).AddMilliseconds($PairTimeoutMs)
        $got = $null
        while ((Get-Date) -lt $deadline)
        {
            if ($Port.BytesToRead -gt 0)
            {
                $tmp = New-Object byte[] $Port.BytesToRead
                [void]$Port.Read($tmp, 0, $tmp.Length)
                if ($TraceRaw) { Write-Host ("  RAW " + (Format-Hex $tmp)) -ForegroundColor Magenta }
                $buffer.AddRange($tmp)
            }
            $got = Get-Sbf20Frame $buffer $index
            if ($got) { break }
            Start-Sleep -Milliseconds 5
        }

        if (-not $got) { throw "timeout waiting for response INDEX=$index (after $PairTimeoutMs ms)" }
        if ($TraceFrames)
        {
            $shown = New-Sbf20Frame $got.Index $got.More $got.Payload
            Write-Host ("  RX " + (Format-Hex $shown)) -ForegroundColor DarkGray
        }

        foreach ($b in $got.Payload) { $response.Add($b) }
        if (-not $got.More) { break }
        $index++
    }

    return [System.Text.Encoding]::UTF8.GetString($response.ToArray())
}

function Write-Result([string]$Json)
{
    Write-Host ('  raw : ' + $Json) -ForegroundColor DarkGray
    try { $obj = $Json | ConvertFrom-Json } catch { Write-Host '  (not json)' -ForegroundColor Yellow; return }

    $code = $obj.code
    $color = if ($code -eq 0) { 'Green' } else { 'Red' }
    $line = "  code: $code"
    if ($obj.PSObject.Properties.Name -contains 'message') { $line += "  message: $($obj.message)" }
    Write-Host $line -ForegroundColor $color

    if ($obj.PSObject.Properties.Name -contains 'params')
    {
        foreach ($p in $obj.params.PSObject.Properties)
        {
            Write-Host ("  {0} = {1}" -f $p.Name, $p.Value)
        }
    }
    if ($obj.PSObject.Properties.Name -contains 'data')
    {
        Write-Host '  data:'
        Write-Host $obj.data
    }
}

function Open-Port
{
    $port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate, 'None', 8, 'One'
    $port.ReadTimeout = 500
    $port.WriteTimeout = 2000
    $port.DtrEnable = [bool]$Dtr
    $port.RtsEnable = [bool]$Rts
    $port.Open()
    $port.DiscardInBuffer()
    $port.DiscardOutBuffer()
    return $port
}

function Test-Probe($Port, [int]$WaitMs)
{
    $Port.DiscardInBuffer()
    $frame = New-Sbf20Frame 0 $false @()
    $Port.Write($frame, 0, $frame.Count)

    $buffer = New-Object System.Collections.Generic.List[byte]
    $deadline = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $deadline)
    {
        if ($Port.BytesToRead -gt 0)
        {
            $tmp = New-Object byte[] $Port.BytesToRead
            [void]$Port.Read($tmp, 0, $tmp.Length)
            $buffer.AddRange($tmp)
        }
        $got = Get-Sbf20Frame $buffer 0
        if ($got) { return [pscustomobject]@{ Ok = $true; Raw = $buffer.ToArray() } }
        Start-Sleep -Milliseconds 5
    }
    return [pscustomobject]@{ Ok = $false; Raw = $buffer.ToArray() }
}

if ($SweepBaud)
{
    Write-Host "== baud sweep on $PortName (DTR=$([bool]$Dtr) RTS=$([bool]$Rts)) ==" -ForegroundColor Cyan
    foreach ($rate in 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600)
    {
        $p = $null
        try
        {
            $p = New-Object System.IO.Ports.SerialPort $PortName, $rate, 'None', 8, 'One'
            $p.ReadTimeout = 500
            $p.WriteTimeout = 2000
            $p.DtrEnable = [bool]$Dtr
            $p.RtsEnable = [bool]$Rts
            $p.Open()
            $p.DiscardInBuffer()
            Start-Sleep -Milliseconds 100
            $r = Test-Probe $p 700
            if ($r.Ok)
            {
                Write-Host ("  {0,7} : PROBE OK" -f $rate) -ForegroundColor Green
            }
            elseif ($r.Raw.Count -gt 0)
            {
                Write-Host ("  {0,7} : {1} bytes -> {2}" -f $rate, $r.Raw.Count, (Format-Hex $r.Raw)) -ForegroundColor Yellow
            }
            else
            {
                Write-Host ("  {0,7} : silence" -f $rate)
            }
        }
        catch
        {
            Write-Host ("  {0,7} : {1}" -f $rate, $_.Exception.Message) -ForegroundColor Red
        }
        finally
        {
            if ($p -and $p.IsOpen) { $p.Close() }
        }
    }
    return
}

$port = $null
try
{
    Write-Host "== SBF-20 link test ==" -ForegroundColor Cyan
    Write-Host "port=$PortName baud=$BaudRate action=$Action"

    $port = Open-Port
    Start-Sleep -Milliseconds 150
    $port.DiscardInBuffer()

    switch ($Action)
    {
        'probe'
        {
            $frame = New-Sbf20Frame 0 $false @()
            Write-Host ('probe frame: ' + (Format-Hex $frame))
            if ($TraceFrames) { Write-Host ("  TX " + (Format-Hex $frame)) -ForegroundColor DarkGray }
            $port.Write($frame, 0, $frame.Count)

            $buffer = New-Object System.Collections.Generic.List[byte]
            $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
            $got = $null
            while ((Get-Date) -lt $deadline)
            {
                if ($port.BytesToRead -gt 0)
                {
                    $tmp = New-Object byte[] $port.BytesToRead
                    [void]$port.Read($tmp, 0, $tmp.Length)
                    if ($TraceRaw) { Write-Host ("  RAW " + (Format-Hex $tmp)) -ForegroundColor Magenta }
                    $buffer.AddRange($tmp)
                }
                $got = Get-Sbf20Frame $buffer 0
                if ($got) { break }
                Start-Sleep -Milliseconds 5
            }

            if ($got) { Write-Host 'PROBE OK - lower board answered' -ForegroundColor Green }
            else
            {
                Write-Host 'PROBE FAILED - no valid response' -ForegroundColor Red
                if ($buffer.Count -gt 0)
                {
                    Write-Host ('  raw bytes: ' + (Format-Hex $buffer.ToArray()))
                    Write-Host ('  as text  : ' + (Format-Bytes $buffer.ToArray()))
                }
                else { Write-Host '  (no bytes received at all)' }
            }
        }

        'read'
        {
            $json = '{"op":"read","configs":["gate","ota","mqtt"]}'
            Write-Host "read request: $json"
            Write-Result (Invoke-Sbf20Session $port $json 1500)
        }

        'readall'
        {
            # no "configs" -> every group (gate + ota + mqtt)
            $json = '{"op":"read"}'
            Write-Host "read all request: $json"
            Write-Result (Invoke-Sbf20Session $port $json 1500)
        }

        'write'
        {
            # only the parameters explicitly given on the command line are sent.
            # MQTT strings (productKey/deviceKey/deviceSecret/mqtt_addr) live in
            # the lower board EEPROM and can be pushed to the module afterwards
            # with -Action config4g (see below).
            $parts = New-Object System.Collections.Generic.List[string]
            if ($PSBoundParameters.ContainsKey('MotorWarningCurrUpper')) { $parts.Add('"motor_warning_curr_upper":' + $MotorWarningCurrUpper) }
            if ($PSBoundParameters.ContainsKey('GateMaxheight')) { $parts.Add('"gate_maxheight":' + $GateMaxheight) }
            if ($PSBoundParameters.ContainsKey('GateMaxturns')) { $parts.Add('"gate_maxturns":' + $GateMaxturns) }
            if ($PSBoundParameters.ContainsKey('GateKaidu')) { $parts.Add('"gate_kaidu":' + $GateKaidu) }
            if ($PSBoundParameters.ContainsKey('GateKaiduCount')) { $parts.Add('"gate_kaidu_count":' + $GateKaiduCount) }
            if ($PSBoundParameters.ContainsKey('OtaFlag')) { $parts.Add('"OTA_flag":' + $OtaFlag) }
            if ($PSBoundParameters.ContainsKey('OtaVer')) { $parts.Add('"OTA_ver":"' + $OtaVer + '"') }
            if ($PSBoundParameters.ContainsKey('UpdateFlag')) { $parts.Add('"UPDATE_flag":' + $UpdateFlag) }
            if ($PSBoundParameters.ContainsKey('ProductKey')) { $parts.Add('"productKey":"' + $ProductKey + '"') }
            if ($PSBoundParameters.ContainsKey('DeviceKey')) { $parts.Add('"deviceKey":"' + $DeviceKey + '"') }
            if ($PSBoundParameters.ContainsKey('DeviceSecret')) { $parts.Add('"deviceSecret":"' + $DeviceSecret + '"') }
            if ($PSBoundParameters.ContainsKey('MqttAddr')) { $parts.Add('"mqtt_addr":"' + $MqttAddr + '"') }
            if ($parts.Count -eq 0) { throw 'no parameter given, nothing to write' }

            $json = '{"op":"write","params":{' + ($parts -join ',') + '}}'
            Write-Host "write request: $json"
            Write-Result (Invoke-Sbf20Session $port $json 5000)
        }

        'paramtest'
        {
            # Round trip over the gate + OTA + MQTT blocks. Values are read back
            # and written unchanged, so the device keeps its current
            # configuration (a blank 0xFF MQTT slot reads as "" and is rewritten
            # as zeroes - same logical value).
            Write-Host '-- step 1/4: read current values' -ForegroundColor Cyan
            $before = Invoke-Sbf20Session $port '{"op":"read"}' 1500
            Write-Result $before
            $p = ($before | ConvertFrom-Json).params
            $json = '{"op":"write","params":{"motor_warning_curr_upper":' + $p.motor_warning_curr_upper +
                    ',"gate_maxheight":' + $p.gate_maxheight +
                    ',"gate_maxturns":' + $p.gate_maxturns +
                    ',"gate_kaidu":' + $p.gate_kaidu +
                    ',"gate_kaidu_count":' + $p.gate_kaidu_count +
                    ',"OTA_flag":' + $p.OTA_flag +
                    ',"OTA_ver":"' + $p.OTA_ver +
                    '","UPDATE_flag":' + $p.UPDATE_flag +
                    ',"productKey":"' + $p.productKey +
                    '","deviceKey":"' + $p.deviceKey +
                    '","deviceSecret":"' + $p.deviceSecret +
                    '","mqtt_addr":"' + $p.mqtt_addr + '"}}'

            Write-Host '-- step 2/4: write the same values back' -ForegroundColor Cyan
            Write-Host "write request: $json"
            Write-Result (Invoke-Sbf20Session $port $json 5000)

            Write-Host '-- step 3/4: read back' -ForegroundColor Cyan
            $after = Invoke-Sbf20Session $port '{"op":"read"}' 1500
            Write-Result $after

            Write-Host '-- step 4/4: compare' -ForegroundColor Cyan
            $q = ($after | ConvertFrom-Json).params
            $fields = 'motor_warning_curr_upper', 'gate_maxheight', 'gate_maxturns', 'gate_kaidu',
                      'gate_kaidu_count', 'OTA_flag', 'OTA_ver', 'OTA_size', 'UPDATE_flag',
                      'productKey', 'deviceKey', 'deviceSecret', 'mqtt_addr'
            $bad = @()
            foreach ($f in $fields)
            {
                if ($p.$f -ne $q.$f) { $bad += ($f + ' (' + $p.$f + ' -> ' + $q.$f + ')') }
            }
            if ($bad.Count -eq 0) { Write-Host 'PARAM TEST OK - write/read round trip matches' -ForegroundColor Green }
            else { Write-Host ('PARAM TEST FAILED - ' + ($bad -join ', ')) -ForegroundColor Red }
        }

        'at'
        {
            $json = '{"op":"at","cmd":"' + $AtCommand + '"}'
            Write-Host "at request: $json"
            Write-Result (Invoke-Sbf20Session $port $json $TimeoutMs)
        }

        'status'
        {
            Write-Host 'status request: {"op":"status"}'
            Write-Result (Invoke-Sbf20Session $port '{"op":"status"}' 1000)
        }

        'at3'
        {
            # AT passthrough to the BLE module on USART3 (DX-BT24)
            $json = '{"op":"at3","cmd":"' + $AtCommand + '"}'
            Write-Host "at3 request: $json"
            Write-Result (Invoke-Sbf20Session $port $json $TimeoutMs)
        }

        'bleaddr'
        {
            $json = '{"op":"at3","cmd":"AT+LADDR"}'
            Write-Host "bleaddr request: $json"
            $resp = Invoke-Sbf20Session $port $json $TimeoutMs
            Write-Result $resp
            if ($resp -match '([0-9A-Fa-f]{2}[-:]){5}[0-9A-Fa-f]{2}')
            {
                $mac = ($Matches[0].ToUpper()) -replace '-', ':'
                Write-Host ("BLE MAC: " + $mac) -ForegroundColor Green
            }
            elseif ($resp -match '(?<![0-9A-Fa-f])([0-9A-Fa-f]{12})(?![0-9A-Fa-f])')
            {
                # DX-BT24 answers +LADDR=48872D8429CB (no separators)
                $hex = $Matches[1].ToUpper()
                $parts = @()
                for ($i = 0; $i -lt 12; $i += 2) { $parts += $hex.Substring($i, 2) }
                Write-Host ("BLE MAC: " + ($parts -join ':')) -ForegroundColor Green
            }
            else
            {
                Write-Host 'BLE MAC: not found in the reply' -ForegroundColor Yellow
            }
        }

        'config4g'
        {
            # The MCU only forwards AT commands now: the 22-command MQTT
            # configuration is built and driven from this side.
            $required = @('ProductKey', 'DeviceKey', 'DeviceSecret', 'MqttAddr')
            foreach ($name in $required)
            {
                if (-not $PSBoundParameters.ContainsKey($name))
                {
                    throw "config4g needs -ProductKey -DeviceKey -DeviceSecret -MqttAddr"
                }
            }

            $server = $MqttAddr
            if ($server -notmatch ',')
            {
                $idx = $server.LastIndexOf(':')
                if ($idx -le 0 -or $idx -eq ($server.Length - 1))
                {
                    throw 'mqtt_addr needs "host:port" or "host,port"'
                }
                $server = $server.Substring(0, $idx) + ',' + $server.Substring($idx + 1)
            }

            $pub = @('up/sys/online', 'up/sys/heartbeat', 'up/property/post', 'up/event/post',
                     'up/alarm/post', 'up/service/reply', 'up/ota/request', 'up/request/invoke')
            $sub = @('down/service/invoke', 'down/config/push', 'down/property/get',
                     'down/ota/reply', 'down/request/reply')

            $cmds = New-Object System.Collections.Generic.List[string]
            $cmds.Add('usr.cn#AT+CSQ?')
            $cmds.Add('usr.cn#AT+WKMOD=MQTT,NOR')
            $cmds.Add("usr.cn#AT+MQTTSVR=$server")
            $cmds.Add("usr.cn#AT+MQTTUSER=$DeviceKey")
            $cmds.Add("usr.cn#AT+MQTTPSW=$DeviceSecret")
            $cmds.Add("usr.cn#AT+MQTTCID=$DeviceKey")
            $cmds.Add('usr.cn#AT+MQTTMOD=1')
            $cmds.Add('usr.cn#AT+HEARTEN=OFF')
            # reconnect policy: 0 means "do not send", the module keeps its value
            if ($SockRstim -ne 0) {
                if ($SockRstim -lt 1 -or $SockRstim -gt 65535) { throw 'SockRstim must be 1..65535' }
                $cmds.Add("usr.cn#AT+SOCKRSTIM=$SockRstim")
            }
            if ($SockRsnum -ne 0) {
                if ($SockRsnum -lt 1 -or $SockRsnum -gt 65535) { throw 'SockRsnum must be 1..65535' }
                $cmds.Add("usr.cn#AT+SOCKRSNUM=$SockRsnum")
            }
            for ($i = 0; $i -lt $pub.Count; $i++)
            {
                $cmds.Add("usr.cn#AT+MQTTPUBTP=$($i + 1),1,iot/v1/$ProductKey/$DeviceKey/$($pub[$i]),0,0,0")
            }
            for ($i = 0; $i -lt $sub.Count; $i++)
            {
                $cmds.Add("usr.cn#AT+MQTTSUBTP=$($i + 1),1,iot/v1/$ProductKey/$DeviceKey/$($sub[$i]),0")
            }
            $cmds.Add('usr.cn#AT+S')

            $total = $cmds.Count
            $failed = $false
            Write-Host ("4G configuration: {0} commands, driven from the PC" -f $total) -ForegroundColor Cyan

            for ($i = 0; $i -lt $total; $i++)
            {
                $cmd = $cmds[$i]
                if ($cmd.Length -gt 128) { throw "command too long ($($cmd.Length)): $cmd" }

                $json = '{"op":"at","cmd":"' + $cmd + '"}'
                $obj = (Invoke-Sbf20Session $port $json 3000) | ConvertFrom-Json
                $data = ''
                if ($obj.PSObject.Properties.Name -contains 'data') { $data = $obj.data }

                $isSave = $cmd.EndsWith('AT+S')
                $okReply = ($data -notmatch 'ERROR') -and (($data -match '\nOK') -or ($data.TrimEnd() -match 'OK$'))

                if ($obj.code -ne 0)
                {
                    if ($isSave -and $obj.code -eq 5)
                    {
                        $okReply = $true   # AT+S saves and restarts the module
                        $data = '(timeout accepted for AT+S)'
                    }
                    else
                    {
                        $detail = if ($obj.PSObject.Properties.Name -contains 'message') { $obj.message } else { '' }
                        Write-Host ("  {0}/{1} FAILED (code {2})  {3}" -f ($i + 1), $total, $obj.code, $cmd) -ForegroundColor Red
                        if ($detail) { Write-Host ("        message: " + $detail) -ForegroundColor Red }
                        if ($data) { Write-Host ("        reply  : " + $data) -ForegroundColor Red }
                        $failed = $true
                        break
                    }
                }
                elseif (-not $okReply)
                {
                    Write-Host ("  {0}/{1} FAILED (no OK)  {2}" -f ($i + 1), $total, $cmd) -ForegroundColor Red
                    Write-Host ("        reply  : " + $data) -ForegroundColor Red
                    $failed = $true
                    break
                }

                Write-Host ("  {0}/{1} OK  {2}" -f ($i + 1), $total, $cmd)
            }

            if ($failed)
            {
                Write-Host '4G CONFIG FAILED' -ForegroundColor Red
            }
            else
            {
                Write-Host '4G CONFIG OK - all 22 AT commands accepted' -ForegroundColor Green
            }
        }

        'readmqtt'
        {
            # query the module for the parameters it actually stores
            $queries = [ordered]@{ 'MQTTSVR' = 'usr.cn#AT+MQTTSVR?'; 'MQTTUSER' = 'usr.cn#AT+MQTTUSER?';
                                   'MQTTPSW' = 'usr.cn#AT+MQTTPSW?'; 'MQTTCID' = 'usr.cn#AT+MQTTCID?';
                                   'SOCKRSTIM' = 'usr.cn#AT+SOCKRSTIM?'; 'SOCKRSNUM' = 'usr.cn#AT+SOCKRSNUM?' }
            foreach ($tag in $queries.Keys)
            {
                $json = '{"op":"at","cmd":"' + $queries[$tag] + '"}'
                $obj = (Invoke-Sbf20Session $port $json 3000) | ConvertFrom-Json
                $data = ''
                if ($obj.PSObject.Properties.Name -contains 'data') { $data = $obj.data }

                $value = ''
                if ($data -match ("\+" + $tag + ":"))
                {
                    $value = ($data -split ("\+" + $tag + ":"))[1]
                    $value = ($value -split "[\r\n]")[0]
                }

                if ($value)
                {
                    Write-Host ("  {0} = {1}" -f $tag, $value) -ForegroundColor Green
                }
                else
                {
                    Write-Host ("  {0} : no value (reply: {1})" -f $tag, ($data -replace "[\r\n]", ' ').Trim()) -ForegroundColor Yellow
                }
            }
        }

        'monitor'
        {
            Write-Host "monitoring $PortName for $TimeoutMs ms (Ctrl+C to stop)..." -ForegroundColor Cyan
            $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
            $buffer = New-Object System.Collections.Generic.List[byte]
            while ((Get-Date) -lt $deadline)
            {
                if ($port.BytesToRead -gt 0)
                {
                    $tmp = New-Object byte[] $port.BytesToRead
                    [void]$port.Read($tmp, 0, $tmp.Length)
                    $buffer.AddRange($tmp)
                }
                Start-Sleep -Milliseconds 20
            }
            if ($buffer.Count -eq 0) { Write-Host '(no bytes)' }
            else
            {
                Write-Host ('bytes: ' + $buffer.Count)
                Write-Host ('hex  : ' + (Format-Hex $buffer.ToArray()))
                Write-Host ('text :')
                Write-Host (Format-Bytes $buffer.ToArray())
            }
        }

        'loopback'
        {
            # Short the adapter TX and RX with a jumper, then run this to prove
            # the adapter + driver + wiring up to the jumper are good.
            $pattern = [System.Text.Encoding]::ASCII.GetBytes('LOOPBACK-TEST-0123456789')
            $port.DiscardInBuffer()
            $port.Write($pattern, 0, $pattern.Length)
            Start-Sleep -Milliseconds 300
            $n = $port.BytesToRead
            if ($n -eq 0)
            {
                Write-Host 'LOOPBACK FAILED - nothing came back (TX/RX not shorted, or adapter TX/RX broken)' -ForegroundColor Red
            }
            else
            {
                $back = New-Object byte[] $n
                [void]$port.Read($back, 0, $n)
                $text = [System.Text.Encoding]::ASCII.GetString($back)
                Write-Host ("echoed: " + $text)
                if ($text.Contains('LOOPBACK-TEST')) { Write-Host 'LOOPBACK OK' -ForegroundColor Green }
                else { Write-Host 'LOOPBACK PARTIAL/NOISY' -ForegroundColor Yellow }
            }
        }
    }
}
finally
{
    if ($port -and $port.IsOpen) { $port.Close() }
}
