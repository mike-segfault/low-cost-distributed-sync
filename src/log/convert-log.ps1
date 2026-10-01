<#
.SYNOPSIS
    Standardizes ESP32/ESP8266MOD UDP communication logs using the ISO 8601 timestamp format.

.DESCRIPTION
    Script converts raw Arduino Serial Monitor logs (text) into ISO 8601
    timestamping standard. Supports the multi-board setup: one ESP32 sender (AP)
    and two receivers (ESP32 = Board 1, ESP8266 = Board 2), with logs from all
    three boards pasted into one input file.

    The device for each line is worked out from its content:
      - Broadcasted / ACK / Completed lines        -> sender
      - Received (board code N)                    -> board N
      - Ignored (addressed to board code N)        -> the OTHER board
      - Startup lines (Connected, UDP port)        -> held until "Board code:" names the board
      - Sent reply / Blink complete / DONE         -> whichever board was last identified

    Use script for time-accurate logs and for following international standards.
#>

#date and timezone
$LogDate = "2026-10-01" #date the logs were captured, change accordingly
$TZOffset = "-04:00" #my local timezone, change accordingly
#input/output files
$InputFile = "log-non-std.txt"
$OutputFile = "multi-board-log.txt"

#network addresses
$SenderPeer = "192.168.4.1:4210" #sender AP, where receivers send OK:/DONE:
$BcastPeer  = "192.168.4.255:4210" #broadcast address the sender transmits to

#board code -> device label (one-hot: 001 = Board 1, 010 = Board 2)
$BoardNames = @{ 1 = "ESP32_B1_STA"; 2 = "ESP8266_B2_STA" }
#board code -> IP; defaults, updated automatically from "Connected. IP:" lines
$BoardIPs = @{ 1 = "192.168.4.2"; 2 = "192.168.4.3" }

#regex patterns (timestamp split off first, then the message is matched)
$tsRe        = [regex] '^(\d\d:\d\d:\d\d\.\d+)\s*->\s*(.*)$'
$broadcastRe = [regex] '^Broadcasted:\s*(CMD:(\d+):(\d+):([\d:,]+))(?:\s*\(retry (\d+)\))?'
$ackRe       = [regex] '^ACK received for seq\s*=\s*(\d+)'
$noAckRe     = [regex] '^No ACK for seq\s*=\s*(\d+)'
$completedRe = [regex] '^Completed seq\s*=\s*(\d+)'
$noDoneRe    = [regex] '^No completion signal for seq\s*=\s*(\d+)'
$recvRe      = [regex] '^Received \(for (?:this board|us)\):\s*(CMD:(\d+):(\d+):([\d:,]+))'
$ignoredRe   = [regex] '^Ignored seq\s*=\s*(\d+) \(addressed to board code (\d+)\)'
$sentRe      = [regex] '^Sent reply:\s*(OK:(\d+))'
$doneRe      = [regex] '^Sent completion:\s*(DONE:(\d+))'
$blinkDoneRe = [regex] '^Blink sequence complete'
$connRe      = [regex] '^Connected\. IP:\s*([\d\.]+)'
$boardCodeRe = [regex] '^Board code:\s*(\d+)'

function Convert-ToISO8601($ts) {
    $dt = [datetime]::ParseExact("$LogDate $ts", "yyyy-MM-dd HH:mm:ss.fff",
                                 [Globalization.CultureInfo]::InvariantCulture)
    return $dt.ToString("yyyy-MM-ddTHH:mm:ss.fff") + $TZOffset
}

function Get-BoardName($code) {
    if ($BoardNames.ContainsKey($code)) { return $BoardNames[$code] }
    return "BOARD_$code"
}

#with two receivers, a board that ignores command is the one it wasn't addressed to
function Get-OtherBoard($code) {
    if ($code -eq 1) { return 2 } else { return 1 }
}

$Output     = [System.Collections.Generic.List[string]]::new()
$device     = "UNKNOWN"
$seqToBoard = @{} #seq -> addressed board code, from broadcasts (used to find who ACKed)
$lastSeq    = @{} #device -> seq it is currently blinking
$pending    = @() #receiver startup lines waiting for "Board code:"
$buffering  = $false

function Flush-Pending($dev) {
    foreach ($p in $script:pending) {
        $script:Output.Add("$(Convert-ToISO8601 $p.ts) $dev EVENT msg=""$($p.msg)"" status=INFO")
    }
    $script:pending = @()
    $script:buffering = $false
}

foreach ($raw in Get-Content $InputFile) {
    $t = $tsRe.Match($raw.Trim())
    if (-not $t.Success) { continue }
    $ts  = $t.Groups[1].Value
    $msg = $t.Groups[2].Value.Trim()
    $iso = Convert-ToISO8601 $ts

    #receiver startup block - hold lines until "Board code:" identifies the board
    if ($msg -match '^Connecting to AP' -or $connRe.IsMatch($msg)) { $buffering = $true }
    if ($buffering) {
        $m = $boardCodeRe.Match($msg)
        if ($m.Success) {
            $code = [int]$m.Groups[1].Value
            $device = Get-BoardName $code
            foreach ($p in $pending) {
                $c = $connRe.Match($p.msg)
                if ($c.Success) { $BoardIPs[$code] = $c.Groups[1].Value }
            }
            Flush-Pending $device
            $Output.Add("$iso $device EVENT msg=""$msg"" status=INFO")
        } else {
            $pending += @{ ts = $ts; msg = $msg }
        }
        continue
    }

    #32 sender broadcast
    $m = $broadcastRe.Match($msg)
    if ($m.Success) {
        $device = "ESP32_AP"
        $seq  = $m.Groups[2].Value
        $code = [int]$m.Groups[3].Value
        $seqToBoard[$seq] = $code
        $retry = if ($m.Groups[5].Success) { " retry=$($m.Groups[5].Value)" } else { "" }
        $Output.Add("$iso ESP32_AP TX UDP peer=$BcastPeer seq=$seq target=$(Get-BoardName $code) payload=""$($m.Groups[1].Value)"" status=SENT$retry")
        continue
    }

    #32 sender received ACK
    $m = $ackRe.Match($msg)
    if ($m.Success) {
        $device = "ESP32_AP"
        $seq = $m.Groups[1].Value
        $peer = if ($seqToBoard.ContainsKey($seq)) { "$($BoardIPs[$seqToBoard[$seq]]):4210" } else { "unknown" }
        $Output.Add("$iso ESP32_AP RX UDP peer=$peer seq=$seq payload=""OK:$seq"" status=ACKED")
        continue
    }

    #32 sender no ACK
    $m = $noAckRe.Match($msg)
    if ($m.Success) {
        $device = "ESP32_AP"
        $Output.Add("$iso ESP32_AP EVENT seq=$($m.Groups[1].Value) status=NO_ACK")
        continue
    }

    #32 sender received DONE (completion confirmed)
    $m = $completedRe.Match($msg)
    if ($m.Success) {
        $device = "ESP32_AP"
        $seq = $m.Groups[1].Value
        $peer = if ($seqToBoard.ContainsKey($seq)) { "$($BoardIPs[$seqToBoard[$seq]]):4210" } else { "unknown" }
        $Output.Add("$iso ESP32_AP RX UDP peer=$peer seq=$seq payload=""DONE:$seq"" status=COMPLETED")
        continue
    }

    #32 sender completion timeout
    $m = $noDoneRe.Match($msg)
    if ($m.Success) {
        $device = "ESP32_AP"
        $Output.Add("$iso ESP32_AP EVENT seq=$($m.Groups[1].Value) status=TIMEOUT")
        continue
    }

    #receiver accepted CMD (board code says which board this is)
    $m = $recvRe.Match($msg)
    if ($m.Success) {
        $seq  = $m.Groups[2].Value
        $code = [int]$m.Groups[3].Value
        $device = Get-BoardName $code
        $lastSeq[$device] = $seq
        $Output.Add("$iso $device RX UDP peer=$SenderPeer seq=$seq payload=""$($m.Groups[1].Value)"" status=RECEIVED")
        continue
    }

    #receiver ignored CMD addressed to the other board
    $m = $ignoredRe.Match($msg)
    if ($m.Success) {
        $seq  = $m.Groups[1].Value
        $code = [int]$m.Groups[2].Value
        $device = Get-BoardName (Get-OtherBoard $code)
        $Output.Add("$iso $device RX UDP peer=$SenderPeer seq=$seq target=$(Get-BoardName $code) status=IGNORED")
        continue
    }

    #receiver sent ACK
    $m = $sentRe.Match($msg)
    if ($m.Success) {
        $Output.Add("$iso $device TX UDP peer=$SenderPeer seq=$($m.Groups[2].Value) payload=""$($m.Groups[1].Value)"" status=SENT")
        continue
    }

    #receiver finished blinking
    if ($blinkDoneRe.IsMatch($msg)) {
        $Output.Add("$iso $device EVENT seq=$($lastSeq[$device]) msg=""Blink sequence complete"" status=COMPLETE")
        continue
    }

    #receiver sent DONE
    $m = $doneRe.Match($msg)
    if ($m.Success) {
        $Output.Add("$iso $device TX UDP peer=$SenderPeer seq=$($m.Groups[2].Value) payload=""$($m.Groups[1].Value)"" status=SENT")
        continue
    }

    #anything else (startup prompts, skipped-command messages) is kept as INFO
    if ($msg -match '^AP IP:') { $device = "ESP32_AP" }
    $Output.Add("$iso $device EVENT msg=""$msg"" status=INFO")
}

#file ended mid-startup without a "Board code:" line
if ($pending.Count -gt 0) { Flush-Pending "UNKNOWN_STA" }

$Output | Set-Content $OutputFile
Write-Host "Conversion complete -> $OutputFile"
