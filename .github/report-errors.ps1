# Publishes the first compiler/linker errors as one workflow annotation, so they can be
# read through the GitHub API without downloading the full log.
param([string]$LogFile, [string]$Title)
if (-not (Test-Path $LogFile)) { Write-Host "::error title=$Title::no error log at $LogFile"; exit 0 }
$lines = Get-Content $LogFile | Where-Object { $_.Trim() -ne "" } |
         ForEach-Object { ($_ -replace '\[[^\]]*\.vcxproj\]', '').Trim() } | Select-Object -Unique
Write-Host "$($lines.Count) unique error lines"
$text = ($lines | Select-Object -First 120) -join "`n"
if ($text.Length -gt 60000) { $text = $text.Substring(0, 60000) }
$enc = $text.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
Write-Host "::error title=$Title::$enc"
