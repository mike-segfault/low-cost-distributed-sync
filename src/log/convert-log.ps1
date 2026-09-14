<#
.SYNOPSIS
    Standardizes ESP32/ESP8266MOD UDP communication logs using the IDO 8601 timestamp format.

.DESCRIPTION
    Script converts raw Arduino Serial Monitor logs (text) into ISO 8601
    timestamping standard.

    Use script for time-accurate logs and for following international standards.
#>

#date and timezone
$LogDate = "2026-09-14"
$TZOffset = "-04:00" #my local timezone, change accordingly
#input/output files
$InputFile = "32-8266-log.md"
$OutputFile = "32-8266-log-std.txt"

#regex patterns
$broadcastRe = [regex] '(\d\d:\d\d:\d\d\.\d+)\s*->\s*Broadcasted:\s*(CMD:\d+:(\d+):[\d\.]+)'
$replyRe = [regex] '(\d\d:\d\d:\d\d\.\d+)\s*->\s*Reply from ([\d\.]+):(\d+)\s*->\s*(OK:(\d+))'
$recvRe = [regex] '(\d\d:\d\d:\d\d\.\d+)\s*->\s*Received from ([\d\.]+):(\d+)\s*->\s*(CMD:\d+:(\d+):[\d\.]+)'
$sentRe = [regex] '(\d\d:\d\d:\d\d\.\d+)\s*->\s*Sent reply:\s*(OK:(\d+))'

function Convert-ToISO8601($ts) {
    $dt = Get-Date "$LogDate $ts"
    return $dt.ToString("yyyy-MM-ddTHH:mm:ss.fff") + $TZOffset
}

$Output = @()
Get-Content $InputFile | ForEach-Object {
    $line = $_.Trim()
    #32 broadcast
    if ($broadcastRe.IsMatch($line)) {
        $m = $broadcastRe.Match($line)
        $ts = $m.Groups[1].Value
        $payload = $m.Groups[2].Value
        $seq = $m.Groups[3].Value

        $Output += "$(Convert-ToISO8601 $ts) ESP32_AP TX UDP peer=192.168.4.255:4210 seq=$seq payload=""$payload"" status=SENT"
        return
    }
    #32 reply received
    if ($replyRe.IsMatch($line)) {
        $m = $replyRe.Match($line)
        $ts = $m.Groups[1].Value
        $ip = $m.Groups[2].Value
        $port = $m.Groups[3].Value
        $payload = $m.Groups[4].Value
        $seq = $m.Groups[5].Value

        $Output += "$(Convert-ToISO8601 $ts) ESP32_AP RX UDP peer=${ip}:$port seq=$seq payload=""$payload"" status=RECEIVED"
        return
    }
    #8266 received CMD
    if ($recvRe.IsMatch($line)) {
        $m = $recvRe.Match($line)
        $ts = $m.Groups[1].Value
        $ip = $m.Groups[2].Value
        $port = $m.Groups[3].Value
        $payload = $m.Groups[4].Value
        $seq = $m.Groups[5].Value

        $Output += "$(Convert-ToISO8601 $ts) ESP8266_STA RX UDP peer=${ip}:$port seq=$seq payload=""$payload"" status=RECEIVED"
        return
    }
    #8266 sent reply
    if ($sentRe.IsMatch($line)) {
        $m = $sentRe.Match($line)
        $ts = $m.Groups[1].Value
        $payload = $m.Groups[2].Value
        $seq = $m.Groups[3].Value

        #8266 always replied to 32 at 192.168.4.1
        $Output += "$(Convert-ToISO8601 $ts) ESP8266_STA TX UDP peer=192.168.4.1:4210 seq=$seq payload=""$payload"" status=SENT"
        return
    }
}

$Output | Set-Content $OutputFile
Write-Host "Conversion complete -> $OutputFile"