param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('listen', 'send')]
    [string]$Action,
    [Parameter(Mandatory = $true)]
    [ValidateSet('dev', 'prod')]
    [string]$Edition
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$script:logPath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'listen.log'

function Write-Log([string]$message) {
    [System.IO.File]::AppendAllText($script:logPath, ((Get-Date).ToString('HH:mm:ss') + ' ' + $message + "`r`n"), [Text.Encoding]::UTF8)
    Write-Output $message
}

$editionName = @{ dev = '研发'; prod = '生产' }[$Edition]
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$localPath = Join-Path $here 'release.local.json'
$statePath = Join-Path $here 'release.state.json'
$notePath = Join-Path $here ("RELEASE-{0}.md" -f $editionName)

if (-not (Test-Path -LiteralPath $localPath)) {
    Write-Output '缺少 release.local.json'
    exit 1
}

$local = Get-Content -LiteralPath $localPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $local.botId -or -not $local.secret) {
    Write-Output 'release.local.json 里没有 Bot ID 或 Secret'
    exit 1
}

function Read-State {
    if (-not (Test-Path -LiteralPath $statePath)) {
        return [pscustomobject]@{ groups = [pscustomobject]@{} }
    }
    return Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Write-State($state) {
    $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $statePath -Encoding UTF8
}

function Send-Frame($socket, $token, $payload) {
    $json = $payload | ConvertTo-Json -Compress -Depth 8
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $segment = New-Object System.ArraySegment[byte] -ArgumentList (, $bytes)
    $socket.SendAsync($segment, [Net.WebSockets.WebSocketMessageType]::Text, $true, $token).Wait()
}

function Receive-Frame($socket, $token, $timeoutMs) {
    $stream = New-Object System.IO.MemoryStream
    $buffer = New-Object byte[] 16384
    $deadline = [datetime]::UtcNow.AddMilliseconds($timeoutMs)
    while ($true) {
        $remain = [int]($deadline - [datetime]::UtcNow).TotalMilliseconds
        if ($remain -le 0) {
            return $null
        }
        $segment = New-Object System.ArraySegment[byte] -ArgumentList (, $buffer)
        $task = $socket.ReceiveAsync($segment, $token)
        try {
            if (-not $task.Wait($remain)) {
                return $null
            }
        } catch {
            $inner = $_.Exception
            while ($inner.InnerException) { $inner = $inner.InnerException }
            $script:receiveError = $inner.Message
            Write-Log ("receive failed: {0}" -f $inner.Message)
            return $null
        }
        $result = $task.Result
        if ($result.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) {
        Write-Log ("socket closed: {0}" -f $socket.CloseStatus)
            return $null
        }
        $stream.Write($buffer, 0, $result.Count)
        if ($result.EndOfMessage) {
            break
        }
    }
    $text = [Text.Encoding]::UTF8.GetString($stream.ToArray())
    if (-not $text) {
        return $null
    }
    return $text | ConvertFrom-Json
}

function Connect-Bot {
    $socket = New-Object System.Net.WebSockets.ClientWebSocket
    $source = New-Object System.Threading.CancellationTokenSource
    $socket.ConnectAsync([Uri]'wss://openws.work.weixin.qq.com', $source.Token).Wait()
    $req = [guid]::NewGuid().ToString('N')
    Send-Frame $socket $source.Token @{
        cmd = 'aibot_subscribe'
        headers = @{ req_id = $req }
        body = @{ bot_id = [string]$local.botId; secret = [string]$local.secret }
    }
    $reply = Receive-Frame $socket $source.Token 15000
    if (-not $reply -or $reply.errcode -ne 0) {
        $code = if ($reply) { $reply.errcode } else { 'timeout' }
        $msg = if ($reply) { $reply.errmsg } else { 'no reply' }
        Write-Log ("subscribe failed {0} {1}" -f $code, $msg)
        $socket.Dispose()
        exit 1
    }
    Write-Log 'bot connected'
    return @{ Socket = $socket; Source = $source }
}

function Get-NoteText {
    if (-not (Test-Path -LiteralPath $notePath)) {
        Write-Output ("缺少 {0}" -f (Split-Path -Leaf $notePath))
        exit 1
    }
    $raw = Get-Content -LiteralPath $notePath -Raw -Encoding UTF8
    $version = ''
    $download = ''
    $changes = New-Object System.Collections.Generic.List[string]
    $notes = New-Object System.Collections.Generic.List[string]
    $section = ''
    foreach ($line in ($raw -split "`r?`n")) {
        if ($line -match '^\s*版本:\s*(.*)$') {
            $version = $Matches[1].Trim()
            $section = ''
        } elseif ($line -match '^\s*下载:\s*(.*)$') {
            $download = $Matches[1].Trim()
            $section = 'download'
        } elseif ($line -match '^\s*变更:\s*(.*)$') {
            $section = 'changes'
            if ($Matches[1].Trim()) { $changes.Add($Matches[1].Trim()) }
        } elseif ($line -match '^\s*说明:\s*(.*)$') {
            $section = 'notes'
            if ($Matches[1].Trim()) { $notes.Add($Matches[1].Trim()) }
        } elseif ($section -eq 'download' -and -not $download -and $line.Trim()) {
            $download = $line.Trim()
            $section = ''
        } elseif ($section -eq 'changes') {
            $text = $line.Trim()
            if (-not $text -or $text -eq '-') { continue }
            $text = $text -replace '^\-\s*', ''
            if ($text) { $changes.Add($text) }
        } elseif ($section -eq 'notes' -and $line.Trim()) {
            $notes.Add($line.Trim())
        }
    }
    if (-not $version -or -not $download -or $changes.Count -eq 0) {
        Write-Output '发布说明里要有版本、下载路径，以及至少一条变更'
        exit 1
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add(('地木升调试助手 {0}版 V{1}' -f $editionName, $version))
    $lines.Add('')
    $lines.Add(('下载：{0}' -f $download))
    $lines.Add('')
    $lines.Add('变更：')
    foreach ($item in $changes) {
        $lines.Add(('- {0}' -f $item))
    }
    if ($notes.Count -gt 0) {
        $lines.Add('')
        $lines.Add('说明：')
        foreach ($item in $notes) {
            $lines.Add($item)
        }
    }
    return ($lines -join "`n")
}

if ($Action -eq 'listen') {
    $conn = Connect-Bot
    Write-Log ("waiting for mention, edition {0}" -f $Edition)
    $socket = $conn.Socket
    $source = $conn.Source
    $lastPing = [datetime]::UtcNow
    $deadline = [datetime]::UtcNow.AddMinutes(4)
    $saved = $false
    $buffer = New-Object byte[] 65536
    while ([datetime]::UtcNow -lt $deadline -and -not $saved) {
        $stream = New-Object System.IO.MemoryStream
        $frame = $null
        while ($null -eq $frame -and [datetime]::UtcNow -lt $deadline) {
            $segment = New-Object System.ArraySegment[byte] -ArgumentList (, $buffer)
            $task = $socket.ReceiveAsync($segment, $source.Token)
            while (-not $task.IsCompleted -and [datetime]::UtcNow -lt $deadline) {
                if (([datetime]::UtcNow - $lastPing).TotalSeconds -ge 25) {
                    Send-Frame $socket $source.Token @{ cmd = 'ping'; headers = @{ req_id = [guid]::NewGuid().ToString('N') } }
                    $lastPing = [datetime]::UtcNow
                }
                Start-Sleep -Milliseconds 200
            }
            if (-not $task.IsCompleted) {
                break
            }
            if ($task.IsFaulted) {
                $inner = $task.Exception
                while ($inner.InnerException) { $inner = $inner.InnerException }
                Write-Log ("receive failed: {0}" -f $inner.Message)
                exit 1
            }
            $result = $task.Result
            if ($result.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) {
                Write-Log ("socket closed: {0}" -f $socket.CloseStatus)
                exit 1
            }
            $stream.Write($buffer, 0, $result.Count)
            if ($result.EndOfMessage) {
                $text = [Text.Encoding]::UTF8.GetString($stream.ToArray())
                if ($text) {
                    $frame = $text | ConvertFrom-Json
                }
            }
        }
        if (-not $frame) {
            break
        }
        $cmd = [string]$frame.cmd
        if ($cmd -ne 'aibot_msg_callback' -and $cmd -ne 'aibot_event_callback') {
            continue
        }
        $chatType = [string]$frame.body.chattype
        $chatId = [string]$frame.body.chatid
        if ($chatType -ne 'group' -or -not $chatId) {
            Write-Log 'ignored non-group event'
            continue
        }
        $state = Read-State
        if (-not $state.groups) {
            $state | Add-Member -NotePropertyName groups -NotePropertyValue ([pscustomobject]@{}) -Force
        }
        $group = [pscustomobject]@{ chatId = $chatId; savedAt = (Get-Date).ToString('s') }
        $state.groups | Add-Member -NotePropertyName $editionName -NotePropertyValue $group -Force
        Write-State $state
        $reqId = [string]$frame.headers.req_id
        if ($reqId) {
            Send-Frame $socket $source.Token @{
                cmd = 'aibot_respond_msg'
                headers = @{ req_id = $reqId }
                body = @{
                    msgtype = 'markdown'
                    markdown = @{ content = ('已连上。之后{0}版发布说明会发到这个群。' -f $editionName) }
                }
            }
            $ack = Receive-Frame $socket $source.Token 10000
            if ($ack -and $ack.errcode -ne 0) {
                Write-Log ("reply failed {0} {1}" -f $ack.errcode, $ack.errmsg)
            }
        }
        Write-Log ("saved group {0}" -f $Edition)
        $saved = $true
    }
    if (-not $saved) {
        Write-Log 'no group message before timeout'
        exit 1
    }
    $socket.Dispose()
    exit 0
}

$state = Read-State
$group = $state.groups.$editionName
if (-not $group -or -not $group.chatId) {
    Write-Output ("group not saved for {0}" -f $Edition)
    exit 1
}
$content = Get-NoteText
$conn = Connect-Bot
$socket = $conn.Socket
$source = $conn.Source
$req = [guid]::NewGuid().ToString('N')
Send-Frame $socket $source.Token @{
    cmd = 'aibot_send_msg'
    headers = @{ req_id = $req }
    body = @{
        chatid = [string]$group.chatId
        chat_type = 2
        msgtype = 'markdown'
        markdown = @{ content = $content }
    }
}
$reply = Receive-Frame $socket $source.Token 15000
$socket.Dispose()
if (-not $reply -or $reply.errcode -ne 0) {
    $code = if ($reply) { $reply.errcode } else { 'timeout' }
    $msg = if ($reply) { $reply.errmsg } else { 'no reply' }
    Write-Output ("发送失败 {0} {1}" -f $code, $msg)
    exit 1
}
Write-Output ("sent {0}" -f $Edition)
exit 0
