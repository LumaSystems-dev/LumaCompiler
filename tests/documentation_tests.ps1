$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'luma.exe'
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temp = Join-Path $tempRoot ('luma-docs-' + [Guid]::NewGuid())
[IO.Directory]::CreateDirectory($temp) | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$lf = [string][char]10
$script:checks = 0
$script:snippets = 0
$script:examples = 0
$script:links = 0

function Invoke-Luma([string[]]$Arguments, [string]$InputText = '') {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $exe
    $info.Arguments = (($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $info.WorkingDirectory = $temp
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $utf8
    $info.StandardErrorEncoding = $utf8
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    $process.Start() | Out-Null
    $output = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()
    $bytes = $utf8.GetBytes($InputText)
    $process.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(20000)) {
        $process.Kill()
        $process.Dispose()
        throw ('Documentation command timeout: ' + ($Arguments -join ' '))
    }
    $result = @{ Code = $process.ExitCode; Out = $output.Result.Replace([string][char]13,''); Err = $errors.Result }
    $process.Dispose()
    return $result
}

function Require-Code($result, $expected, $label) {
    if ($result.Code -ne $expected) {
        throw "$label failed: expected=$expected exit=$($result.Code) stdout=$($result.Out) stderr=$($result.Err)"
    }
    $script:checks++
}

function Check-Program($path, $label, $inputText = '', $mode = 'program', $expectedOutput = '') {
    if ($mode -eq 'eval') {
        $result = Invoke-Luma @('eval',$path) $inputText
        Require-Code $result 0 $label
        if ($expectedOutput -and $result.Out.Trim() -ne $expectedOutput) { throw "$label output mismatch" }
    } elseif ($mode -eq 'error') {
        foreach ($command in @('run','vm')) {
            $argsForCommand = @($command,$path)
            if ($command -eq 'compile') { $argsForCommand += (Join-Path $temp 'negative.lbc') }
            $result = Invoke-Luma $argsForCommand $inputText
            Require-Code $result 65 "$label $command"
            if ($expectedOutput -and $result.Err -notlike ('*' + $expectedOutput + '*')) { throw "$label error mismatch" }
        }
        $artifact = Join-Path $temp 'negative.lbc'
        $compiled = Invoke-Luma @('compile',$path,$artifact)
        # Runtime failures compile successfully; frontend failures do not.
        if ($compiled.Code -eq 0) {
            Require-Code $compiled 0 "$label compile"
            $executed = Invoke-Luma @('execute',$artifact) $inputText
            Require-Code $executed 65 "$label execute"
        } else {
            Require-Code $compiled 65 "$label compile"
        }
    } else {
        $interpreted = Invoke-Luma @('run',$path) $inputText
        $vm = Invoke-Luma @('vm',$path) $inputText
        Require-Code $interpreted 0 "$label run"
        Require-Code $vm 0 "$label vm"
        if ($interpreted.Out -ne $vm.Out) { throw "$label backend outputs differ" }
        if ($expectedOutput -and $vm.Out -notlike ('*' + $expectedOutput + '*')) { throw "$label expected output absent" }
        $artifact = Join-Path $temp 'checked.lbc'
        Require-Code (Invoke-Luma @('compile',$path,$artifact)) 0 "$label compile"
        $executed = Invoke-Luma @('execute',$artifact) $inputText
        Require-Code $executed 0 "$label execute"
        if ($executed.Out -ne $vm.Out) { throw "$label artifact output differs" }
        Require-Code (Invoke-Luma @('bytecode',$artifact)) 0 "$label bytecode"
    }
    Write-Host "[ OK ] $label"
}

try {
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Build luma.exe first.' }
    Copy-Item -LiteralPath (Join-Path $root 'stdlib') -Destination $temp -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'examples') -Destination $temp -Recurse

    $documents = Get-ChildItem -LiteralPath $root -Filter '*.md' -Recurse -File
    foreach ($doc in $documents) {
        $content = [IO.File]::ReadAllText($doc.FullName, $utf8)
        $pattern = '(?ms)(?:<!-- luma-check: ([^\r\n]*?) -->\s*)?^\x60{3}luma[ \t]*\r?\n(.*?)^\x60{3}[ \t]*\r?$'
        foreach ($block in [regex]::Matches($content,$pattern)) {
            $script:snippets++
            $directive = $block.Groups[1].Value
            $base = Join-Path $temp 'examples'
            $mode = 'program'
            $inputText = ''
            if ($directive -eq 'root') { $base = $temp }
            elseif ($directive -eq 'error' -or $directive -eq 'eval') { $mode = $directive }
            elseif ($directive.StartsWith('input ')) {
                $inputText = ($directive.Substring(6).Split('|') -join $lf) + $lf
            } elseif ($directive) { throw "Unknown luma-check directive: $directive" }
            $path = Join-Path $base ("documentation-$($script:snippets).luma")
            [IO.File]::WriteAllText($path,$block.Groups[2].Value,$utf8)
            Check-Program $path "$($doc.Name) snippet $($script:snippets)" $inputText $mode
        }

        foreach ($link in [regex]::Matches($content,'\[[^\]\r\n]+\]\(([^)\r\n]+)\)')) {
            $target = $link.Groups[1].Value.Trim('<','>')
            if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') { continue }
            $parts = $target.Split('#',2)
            $pathPart = [Uri]::UnescapeDataString($parts[0])
            $linkedFile = $doc.FullName
            if ($pathPart) { $linkedFile = Join-Path $doc.DirectoryName $pathPart }
            if (-not (Test-Path -LiteralPath $linkedFile)) { throw "Broken link in $($doc.Name): $target" }
            if ($parts.Length -eq 2 -and $parts[1]) {
                $linkedText = [IO.File]::ReadAllText($linkedFile,$utf8)
                $anchor = [Uri]::UnescapeDataString($parts[1])
                $anchors = @()
                foreach ($heading in [regex]::Matches($linkedText,'(?m)^#{1,6} (.+?)\r?$')) {
                    $slug = $heading.Groups[1].Value.ToLowerInvariant() -replace '[^\p{L}\p{N}_\- ]',''
                    $anchors += ($slug -replace ' ','-')
                }
                if ($anchors -notcontains $anchor -and $linkedText -notmatch ('id="' + [regex]::Escape($anchor) + '"')) {
                    throw "Broken anchor in $($doc.Name): $target"
                }
            }
            $script:links++
        }
    }

    $rpgMoves = (@('Astra','mage','special','special','special','special','special','heal','special','special') -join $lf) + $lf
    foreach ($source in Get-ChildItem -LiteralPath (Join-Path $root 'examples') -Filter '*.luma' -Recurse -File) {
        $relative = $source.FullName.Substring($root.Length + 1)
        $path = Join-Path $temp $relative
        $mode = 'program'
        $inputText = ''
        $expectedOutput = ''
        switch ($relative.Replace('\','/')) {
            'examples/analyzer_demo.luma' { $mode = 'error'; $expectedOutput = "Unknown variable 'score'" }
            'examples/eval_demo.luma' { $mode = 'eval'; $expectedOutput = '9!' }
            'examples/input_demo.luma' { $inputText = (@('Ada','21') -join $lf) + $lf; $expectedOutput = '22' }
            'examples/HELLOWORLD.luma' { $inputText = '3' + $lf }
            'examples/rpg_demo.luma' { $inputText = (@('Ada','mage','attack','attack','attack') -join $lf) + $lf }
            'examples/rpg_adventure/main.luma' { $inputText = $rpgMoves; $expectedOutput = '50' }
            'examples/stdlib_demo.luma' { $inputText = (@('  Hero Name  ','1') -join $lf) + $lf; $expectedOutput = 'HP=16' }
        }
        Check-Program $path $relative $inputText $mode $expectedOutput
        $script:examples++
    }
    Require-Code (Invoke-Luma @('tokens',(Join-Path $temp 'examples/lex_demo.luma'))) 0 'tokens example'
    Require-Code (Invoke-Luma @('ast',(Join-Path $temp 'examples/ast_demo.luma'))) 0 'ast example'
    Require-Code (Invoke-Luma @('bytecode',(Join-Path $temp 'examples/bytecode_demo.luma'))) 0 'bytecode source example'
    Require-Code (Invoke-Luma @('--version')) 0 'version'
    Require-Code (Invoke-Luma @('--help')) 0 'help'
    Write-Host "$($script:snippets) Markdown snippets, $($script:examples) example files, $($script:links) local links; $($script:checks) command checks passed."
} finally {
    # Only delete this script's newly created, validated GUID temporary directory.
    $resolvedTemp = [IO.Path]::GetFullPath($temp)
    $tempPrefix = $tempRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if ($resolvedTemp.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemp) -match '^luma-docs-[0-9a-f-]{36}$') {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
    }
}
