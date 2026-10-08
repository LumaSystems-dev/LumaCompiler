$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'luma.exe'
$temp = Join-Path ([IO.Path]::GetTempPath()) ('luma-cli-' + [Guid]::NewGuid())
[IO.Directory]::CreateDirectory($temp) | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$script:passed = 0
function Write-Source($name, $text) {
    [IO.File]::WriteAllText((Join-Path $temp $name), $text, $utf8)
}
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
        throw 'CLI timeout'
    }
    $result = @{ Code = $process.ExitCode; Out = $output.Result.Replace("`r", ''); Err = $errors.Result }
    $process.Dispose()
    return $result
}
function Assert-Result($result, $code, $contains, $name) {
    if ($result.Code -ne $code -or ($result.Out + $result.Err) -notlike ('*' + $contains + '*')) {
        throw "$name failed: exit=$($result.Code), stdout=$($result.Out), stderr=$($result.Err)"
    }
    $script:passed++
    Write-Host "[ OK ] $name"
}
try {
    Assert-Result (Invoke-Luma @('--version')) 0 '0.2.0' 'version'
    Assert-Result (Invoke-Luma @('--help')) 0 'Usage:' 'help'
    Assert-Result (Invoke-Luma @()) 64 'Usage:' 'no arguments'
    Assert-Result (Invoke-Luma @('nonsense')) 64 'Unknown command' 'unknown command'
    Assert-Result (Invoke-Luma @('vm', 'absent.luma')) 66 'Cannot open file' 'missing source'
    Assert-Result (Invoke-Luma @('--version', 'extra')) 64 'Usage:' 'wrong arguments'
    Write-Source 'math.luma' "function add(a,b)`nreturn a+b`nend`n"
    Write-Source 'nested.luma' "import `"./math.luma`"`n"
    Write-Source 'main.luma' "import `"nested.luma`"`nimport `"math.luma`"`nprint(add(20,22))`nprint(input())`n"
    foreach ($mode in @('run', 'vm')) {
        Assert-Result (Invoke-Luma @($mode, 'main.luma') "hello`n") 0 "42`nhello" "$mode nested duplicate import/input"
    }
    Assert-Result (Invoke-Luma @('main.luma') "hello`n") 0 "42`nhello" 'implicit VM'
    Assert-Result (Invoke-Luma @('compile', 'main.luma')) 0 'compiled' 'compile'
    Assert-Result (Invoke-Luma @('execute', 'main.lbc') "hello`n") 0 "42`nhello" 'execute'
    Assert-Result (Invoke-Luma @('bytecode', 'main.luma')) 0 'CALL' 'source disassembly'
    Assert-Result (Invoke-Luma @('bytecode', 'main.lbc')) 0 'CALL' 'artifact disassembly'
    Assert-Result (Invoke-Luma @('main.lbc') "hello`n") 0 "42`nhello" 'implicit artifact execution'
    $unicode = -join ([char[]](0x041B,0x0443,0x043C,0x0430))
    Assert-Result (Invoke-Luma @('vm','main.luma') ($unicode + "`n")) 0 $unicode 'Unicode CLI input'
    $longInput = 'x' * 12000
    Assert-Result (Invoke-Luma @('execute','main.lbc') ($longInput + "`n")) 0 $longInput 'long artifact input'
    Write-Source 'missing.luma' 'import "does-not-exist.luma"'
    foreach ($mode in @('run','vm','compile','bytecode')) {
        Assert-Result (Invoke-Luma @($mode,'missing.luma')) 65 'not found' "$mode missing module"
    }
    Write-Source 'a.luma' "import `"b.luma`""
    Write-Source 'b.luma' "import `"a.luma`""
    foreach ($mode in @('run','vm','compile','bytecode')) {
        Assert-Result (Invoke-Luma @($mode, 'a.luma')) 65 'circular import' "$mode circular import"
    }
    Write-Source 'broken.luma' 'print(missing)'
    Assert-Result (Invoke-Luma @('vm','broken.luma')) 65 '^' 'identifier source location'
    Write-Source 'num.luma' 'print(num(input()))'
    Assert-Result (Invoke-Luma @('vm','num.luma') "hello`n") 65 'cannot convert' 'invalid num input'
    Assert-Result (Invoke-Luma @('vm','num.luma') "-42`n") 0 '-42' 'negative num input'
    Assert-Result (Invoke-Luma @('vm','num.luma') "`n") 65 'cannot convert' 'empty num input'
    Write-Source 'syntax.luma' 'let x = ;'
    Write-Source 'syntax-main.luma' 'import "syntax.luma"'
    Assert-Result (Invoke-Luma @('vm','syntax-main.luma')) 65 'syntax.luma:1:9' 'imported syntax location'
    Write-Source 'error.luma' 'print(1 + "bad")'
    Assert-Result (Invoke-Luma @('vm','error.luma')) 65 'cannot add' 'runtime error'
    Write-Source 'hello.luma' 'print("Hello World")'
    Assert-Result (Invoke-Luma @('vm','hello.luma')) 0 'Hello World' 'hello world'
    [IO.File]::WriteAllBytes((Join-Path $temp 'corrupt.lbc'), [byte[]](76,66,67,2))
    Assert-Result (Invoke-Luma @('execute','corrupt.lbc')) 65 'format version' 'corrupt artifact'
    $invalidOpcode = [byte[]](76,66,67,1,1,0,0,0,255,1,0,0,0,1,0,0,0,0,0,0,0)
    [IO.File]::WriteAllBytes((Join-Path $temp 'opcode.lbc'), $invalidOpcode)
    Assert-Result (Invoke-Luma @('execute','opcode.lbc')) 65 'unknown opcode' 'malformed opcode artifact'
    Remove-Item -LiteralPath (Join-Path $temp 'math.luma'),(Join-Path $temp 'nested.luma'),(Join-Path $temp 'main.luma')
    Assert-Result (Invoke-Luma @('execute','main.lbc') "hello`n") 0 "42`nhello" 'artifact independent of sources'
    Assert-Result (Invoke-Luma @('execute','absent.lbc')) 66 'Cannot open file' 'missing artifact'
    foreach ($example in @('first_program','functions_demo','if_demo','loop_demo','array_demo')) {
        Assert-Result (Invoke-Luma @('vm', (Join-Path $root "examples\$example.luma"))) 0 '' "example $example"
    }
    $rpg = Join-Path $root 'examples\rpg_adventure\main.luma'
    $moves = "Astra`nmage`nspecial`nspecial`nspecial`nspecial`nspecial`nheal`nspecial`nspecial`n"
    $source = Invoke-Luma @('vm',$rpg) $moves
    Assert-Result $source 0 '50' 'RPG source victory'
    Assert-Result (Invoke-Luma @('compile',$rpg,(Join-Path $temp 'rpg.lbc'))) 0 'compiled' 'RPG compile'
    $artifact = Invoke-Luma @('execute','rpg.lbc') $moves
    Assert-Result $artifact 0 '50' 'RPG artifact victory'
    if ($source.Out -ne $artifact.Out) { throw 'RPG source/artifact output differs' }
    # Multi-file consumers share modules through the existing resolver.
    [IO.Directory]::CreateDirectory((Join-Path $temp 'lib')) | Out-Null
    foreach ($module in @('math','string','random','io')) {
        Copy-Item -LiteralPath (Join-Path $root "stdlib\$module.luma") -Destination (Join-Path $temp "lib\$module.luma")
    }
    Write-Source 'consumer-a.luma' "import `"lib/math.luma`"`nimport `"lib/random.luma`"`nfunction score(n)`nreturn abs(n)+random_range(1,1)`nend"
    Write-Source 'consumer-b.luma' "import `"lib/math.luma`"`nimport `"lib/string.luma`"`nfunction report(n)`nreturn join([`"score`",str(n)],`":`")`nend"
    Write-Source 'library-main.luma' "import `"consumer-a.luma`"`nimport `"consumer-b.luma`"`nimport `"lib/../lib/math.luma`"`nprint(report(score(-4)))"
    foreach ($mode in @('run','vm')) {
        Assert-Result (Invoke-Luma @($mode,'library-main.luma')) 0 'score:5' "$mode multifile stdlib"
    }
    Assert-Result (Invoke-Luma @('library-main.luma')) 0 'score:5' 'implicit stdlib source'
    Assert-Result (Invoke-Luma @('bytecode','library-main.luma')) 0 'CALL' 'stdlib disassembly'
    Assert-Result (Invoke-Luma @('compile','library-main.luma')) 0 'compiled' 'stdlib compile'
    Assert-Result (Invoke-Luma @('execute','library-main.lbc')) 0 'score:5' 'stdlib execute'
    Write-Source 'library-error.luma' "import `"lib/math.luma`"`nprint(sqrt(-1))"
    Assert-Result (Invoke-Luma @('vm','library-error.luma')) 65 'nonnegative' 'stdlib domain error'
    Write-Source 'library-arity.luma' "import `"lib/math.luma`"`nabs()"
    Assert-Result (Invoke-Luma @('vm','library-arity.luma')) 65 'expects 1 arguments' 'stdlib arity error'
    Write-Source 'library-conflict.luma' "import `"lib/math.luma`"`nlet abs = 1"
    Assert-Result (Invoke-Luma @('compile','library-conflict.luma')) 65 'already defined' 'stdlib global conflict'
    $demo = Join-Path $root 'examples\stdlib_demo.luma'
    $demoInput = "  Hero Name  `n1`n"
    $interpretedDemo = Invoke-Luma @('run',$demo) $demoInput
    $vmDemo = Invoke-Luma @('vm',$demo) $demoInput
    Assert-Result $interpretedDemo 0 'HP=16' 'stdlib demo interpreter'
    Assert-Result $vmDemo 0 'HP=16' 'stdlib demo VM'
    if ($interpretedDemo.Out -ne $vmDemo.Out) { throw 'stdlib demo backends differ' }
    Assert-Result (Invoke-Luma @('compile',$demo,(Join-Path $temp 'demo.lbc'))) 0 'compiled' 'stdlib demo compile'
    $artifactDemo = Invoke-Luma @('execute','demo.lbc') $demoInput
    Assert-Result $artifactDemo 0 'HP=16' 'stdlib demo artifact'
    if ($artifactDemo.Out -ne $vmDemo.Out) { throw 'stdlib demo artifact differs' }
    # The artifact has inlined the modules: deleting their copied source files
    # must not change execution (do not touch the project's real stdlib).
    foreach ($module in @('math','string','random','io')) {
        Remove-Item -LiteralPath (Join-Path $temp "lib\$module.luma")
    }
    Assert-Result (Invoke-Luma @('execute','library-main.lbc')) 0 'score:5' 'stdlib artifact without modules'
    Write-Host "$script:passed CLI tests passed, 0 failed"
} finally {
    # Only remove this test's GUID-named directory under the OS temp directory.
    if ([IO.Path]::GetFullPath($temp).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()))) {
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}
