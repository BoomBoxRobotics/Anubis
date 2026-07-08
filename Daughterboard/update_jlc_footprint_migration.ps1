$ErrorActionPreference = 'Stop'

$PcbPath = 'Daughterboard.kicad_pcb'
$SchPath = 'Daughterboard.kicad_sch'
$KiCadFpRoot = 'C:\Program Files\KiCad\10.0\share\kicad\footprints'

function Get-BlockAt {
    param([string]$Text, [int]$Start)
    $depth = 0
    for ($i = $Start; $i -lt $Text.Length; $i++) {
        $c = $Text[$i]
        if ($c -eq '(') { $depth++ }
        elseif ($c -eq ')') {
            $depth--
            if ($depth -eq 0) {
                return [pscustomobject]@{ Start = $Start; End = $i; Text = $Text.Substring($Start, $i - $Start + 1) }
            }
        }
    }
    throw "Unclosed block at $Start"
}

function Find-FootprintBlock {
    param([string]$Text, [string]$Ref)
    $needle = '"Reference" "' + $Ref + '"'
    $prop = $Text.IndexOf($needle)
    if ($prop -lt 0) { throw "Footprint reference $Ref not found" }
    $start = $Text.LastIndexOf('(footprint', $prop)
    if ($start -lt 0) { throw "Footprint block for $Ref not found" }
    return Get-BlockAt -Text $Text -Start $start
}

function Get-FirstMatch {
    param([string]$Text, [string]$Pattern, [string]$Default = '')
    $m = [regex]::Match($Text, $Pattern)
    if ($m.Success) { return $m.Groups[1].Value }
    return $Default
}

function Replace-PropertyValue {
    param([string]$Text, [string]$Property, [string]$Value)
    $escaped = [regex]::Escape($Property)
    return [regex]::Replace(
        $Text,
        '(\(property "' + $escaped + '" ")([^"]*)(")',
        { param($m) $m.Groups[1].Value + $Value + $m.Groups[3].Value },
        1
    )
}

function Replace-Or-Append-FpProperty {
    param([string]$Block, [string]$Property, [string]$Value)
    if ($Block -match [regex]::Escape('(property "' + $Property + '"')) {
        return Replace-PropertyValue -Text $Block -Property $Property -Value $Value
    }
    $insertAfter = $Block.IndexOf("`n`t(attr ")
    if ($insertAfter -lt 0) { return $Block }
    $propText = "`t(property `"$Property`" `"$Value`"`n`t`t(at 0 0 0)`n`t`t(layer `"F.Fab`")`n`t`t(hide yes)`n`t`t(effects`n`t`t`t(font`n`t`t`t`t(size 1 1)`n`t`t`t`t(thickness 0.15)`n`t`t`t)`n`t`t)`n`t)`n"
    return $Block.Insert($insertAfter + 1, $propText)
}

function Get-PadNets {
    param([string]$Block)
    $map = @{}
    $idx = 0
    while (($idx = $Block.IndexOf('(pad ', $idx)) -ge 0) {
        $pad = Get-BlockAt -Text $Block -Start $idx
        $num = Get-FirstMatch -Text $pad.Text -Pattern '\(pad "([^"]+)"'
        $net = Get-FirstMatch -Text $pad.Text -Pattern '\(net "([^"]+)"\)'
        if ($num -and $net) { $map[$num] = $net }
        $idx = $pad.End + 1
    }
    return $map
}

function Add-NetAndUuid-ToPads {
    param([string]$Block, [hashtable]$PadNets)
    $out = New-Object System.Text.StringBuilder
    $pos = 0
    while ($true) {
        $idx = $Block.IndexOf('(pad ', $pos)
        if ($idx -lt 0) {
            [void]$out.Append($Block.Substring($pos))
            break
        }
        [void]$out.Append($Block.Substring($pos, $idx - $pos))
        $pad = Get-BlockAt -Text $Block -Start $idx
        $padText = [regex]::Replace($pad.Text, "`n\s*\(net ""[^""]+""\)", '')
        $padText = [regex]::Replace($padText, "`n\s*\(uuid ""[^""]+""\)", '')
        $num = Get-FirstMatch -Text $padText -Pattern '\(pad "([^"]+)"'
        $insert = "`n`t`t`t(uuid `"$([guid]::NewGuid().ToString())`")"
        if ($PadNets.ContainsKey($num)) {
            $insert = "`n`t`t`t(net `"$($PadNets[$num])`")" + $insert
        }
        $close = $padText.LastIndexOf("`n`t`t)")
        if ($close -lt 0) { $close = $padText.LastIndexOf(')') }
        $padText = $padText.Insert($close, $insert)
        [void]$out.Append($padText)
        $pos = $pad.End + 1
    }
    return $out.ToString()
}

function New-FootprintBlock {
    param(
        [string]$LibPath,
        [string]$OldBlock,
        [string]$Ref,
        [string]$Value,
        [hashtable]$PadNets,
        [hashtable]$ExtraProperties = @{}
    )
    $lib = Get-Content -LiteralPath $LibPath -Raw
    $lib = [regex]::Replace($lib, "(?m)^\t\((version|generator|generator_version)[^\r\n]*\r?\n", '')
    $at = Get-FirstMatch -Text $OldBlock -Pattern "`n\s*\(at ([^)]+)\)"
    $uuid = Get-FirstMatch -Text $OldBlock -Pattern "`n\s*\(uuid ""([^""]+)""\)"
    if (-not $uuid) { $uuid = [guid]::NewGuid().ToString() }
    $lib = $lib -replace "`n`t\(layer `"F\.Cu`"\)", "`n`t(layer `"F.Cu`")`n`t(uuid `"$uuid`")`n`t(at $at)"
    $lib = Replace-PropertyValue -Text $lib -Property 'Reference' -Value $Ref
    $lib = Replace-PropertyValue -Text $lib -Property 'Value' -Value $Value
    foreach ($key in $ExtraProperties.Keys) {
        $lib = Replace-Or-Append-FpProperty -Block $lib -Property $key -Value $ExtraProperties[$key]
    }
    $lib = Add-NetAndUuid-ToPads -Block $lib -PadNets $PadNets
    return $lib
}

function Replace-Footprint {
    param([string]$Text, [string]$Ref, [string]$NewBlock)
    $old = Find-FootprintBlock -Text $Text -Ref $Ref
    return $Text.Substring(0, $old.Start) + $NewBlock + $Text.Substring($old.End + 1)
}

function Remove-RoutingNear {
    param([string]$Text, [array]$Centers)
    $removedSegments = 0
    $removedVias = 0
    $out = New-Object System.Text.StringBuilder
    $pos = 0
    $starts = @()
    foreach ($kind in @('(segment','(via')) {
        $idx = 0
        while (($idx = $Text.IndexOf($kind, $idx)) -ge 0) {
            $starts += [pscustomobject]@{ Index = $idx; Kind = $kind }
            $idx += 1
        }
    }
    $starts = $starts | Sort-Object Index
    foreach ($s in $starts) {
        if ($s.Index -lt $pos) { continue }
        [void]$out.Append($Text.Substring($pos, $s.Index - $pos))
        $b = Get-BlockAt -Text $Text -Start $s.Index
        $remove = $false
        $points = @()
        foreach ($m in [regex]::Matches($b.Text, '\((?:start|end|at) ([\-0-9.]+) ([\-0-9.]+)')) {
            $points += [pscustomobject]@{ X = [double]$m.Groups[1].Value; Y = [double]$m.Groups[2].Value }
        }
        foreach ($p in $points) {
            foreach ($c in $Centers) {
                $dx = $p.X - $c.X
                $dy = $p.Y - $c.Y
                if ([math]::Sqrt($dx*$dx + $dy*$dy) -le $c.Radius) { $remove = $true; break }
            }
            if ($remove) { break }
        }
        if ($remove) {
            if ($s.Kind -eq '(segment') { $removedSegments++ } else { $removedVias++ }
        } else {
            [void]$out.Append($b.Text)
        }
        $pos = $b.End + 1
    }
    [void]$out.Append($Text.Substring($pos))
    return [pscustomobject]@{ Text = $out.ToString(); Segments = $removedSegments; Vias = $removedVias }
}

$pcb = Get-Content -LiteralPath $PcbPath -Raw

$r4Old = Find-FootprintBlock -Text $pcb -Ref 'R4'
$r4Nets = Get-PadNets -Block $r4Old.Text
$r4Block = New-FootprintBlock `
    -LibPath (Join-Path $KiCadFpRoot 'Resistor_SMD.pretty\R_2512_6332Metric.kicad_mod') `
    -OldBlock $r4Old.Text `
    -Ref 'R4' `
    -Value '43R 1W CBSET' `
    -PadNets $r4Nets `
    -ExtraProperties @{ 'LCSC Part #' = 'C38957'; 'Manufacturer Part Number' = '25121WJ0430T4E' }
$pcb = Replace-Footprint -Text $pcb -Ref 'R4' -NewBlock $r4Block

$q1Old = Find-FootprintBlock -Text $pcb -Ref 'Q1'
$q1Nets = @{
    '1' = 'BATT_RAW_N'
    '2' = 'DO_GATE'
    '3' = 'GND'
    '4' = 'CO_GATE'
    '5' = 'Q1_DRAIN_COMMON'
    '6' = 'Q1_DRAIN_COMMON'
    '7' = 'Q1_DRAIN_COMMON'
    '8' = 'Q1_DRAIN_COMMON'
}
$q1Block = New-FootprintBlock `
    -LibPath (Join-Path $KiCadFpRoot 'Package_SO.pretty\TSSOP-8_4.4x3mm_P0.65mm.kicad_mod') `
    -OldBlock $q1Old.Text `
    -Ref 'Q1' `
    -Value 'PT8810 dual N-MOSFET' `
    -PadNets $q1Nets `
    -ExtraProperties @{ 'LCSC Part #' = 'C3019811'; 'Manufacturer Part Number' = 'PT8810' }
$pcb = Replace-Footprint -Text $pcb -Ref 'Q1' -NewBlock $q1Block

$j5Old = Find-FootprintBlock -Text $pcb -Ref 'J5'
$j5Block = New-FootprintBlock `
    -LibPath (Join-Path $KiCadFpRoot 'Connector_PinHeader_2.54mm.pretty\PinHeader_1x04_P2.54mm_Vertical_SMD_Pin1Left.kicad_mod') `
    -OldBlock $j5Old.Text `
    -Ref 'J5' `
    -Value 'I2C SMD male header 2.54mm' `
    -PadNets (Get-PadNets -Block $j5Old.Text) `
    -ExtraProperties @{ 'LCSC Part #' = 'C41417361'; 'Manufacturer Part Number' = 'HX PZ2.54-1x4P TP-YQ' }
$pcb = Replace-Footprint -Text $pcb -Ref 'J5' -NewBlock $j5Block

$j6Old = Find-FootprintBlock -Text $pcb -Ref 'J6'
$j6Block = New-FootprintBlock `
    -LibPath (Join-Path $KiCadFpRoot 'Connector_PinHeader_2.54mm.pretty\PinHeader_1x02_P2.54mm_Vertical_SMD_Pin1Left.kicad_mod') `
    -OldBlock $j6Old.Text `
    -Ref 'J6' `
    -Value 'Deep sleep SMD pushbutton header' `
    -PadNets (Get-PadNets -Block $j6Old.Text) `
    -ExtraProperties @{ 'LCSC Part #' = 'C41417359'; 'Manufacturer Part Number' = 'HX PZ2.54-1x2P TP-YQ' }
$pcb = Replace-Footprint -Text $pcb -Ref 'J6' -NewBlock $j6Block

$centers = @(
    [pscustomobject]@{ X = 33.175; Y = 39.3; Radius = 5.0 },
    [pscustomobject]@{ X = 48.55; Y = 44.275; Radius = 6.0 },
    [pscustomobject]@{ X = 122.6; Y = 24.8; Radius = 8.0 },
    [pscustomobject]@{ X = 123.0; Y = 38.86; Radius = 6.0 }
)
$routeResult = Remove-RoutingNear -Text $pcb -Centers $centers
$pcb = $routeResult.Text
Set-Content -LiteralPath $PcbPath -Value $pcb -Encoding utf8

$sch = Get-Content -LiteralPath $SchPath -Raw

$oldSymbolStart = $sch.IndexOf('(symbol "Daughterboard:PROT_FET_PAIR"')
if ($oldSymbolStart -lt 0) { throw 'Old Q1 symbol definition not found' }
$oldSymbol = Get-BlockAt -Text $sch -Start $oldSymbolStart
$newSymbol = @'
(symbol "Daughterboard:PT8810_TSSOP8"
			(exclude_from_sim no)
			(in_bom yes)
			(on_board yes)
			(duplicate_pin_numbers_are_jumpers no)
			(property "Reference" "Q"
				(at -15.24 -22.86 0)
				(effects (font (size 1.27 1.27)))
			)
			(property "Value" "PT8810"
				(at -15.24 22.86 0)
				(effects (font (size 1.27 1.27)))
			)
			(property "Footprint" "Package_SO:TSSOP-8_4.4x3mm_P0.65mm"
				(at 0 0 0)
				(effects (font (size 1.27 1.27)) (hide yes))
			)
			(property "Datasheet" ""
				(at 0 0 0)
				(effects (font (size 1.27 1.27)) (hide yes))
			)
			(property "Description" "TSSOP-8 dual N-channel MOSFET used as the 2S protection back-to-back FET pair."
				(at 0 0 0)
				(effects (font (size 1.27 1.27)) (hide yes))
			)
			(symbol "PT8810_TSSOP8_0_1"
				(rectangle (start -15.24 -15.24) (end 15.24 15.24) (stroke (width 0.254) (type default)) (fill (type background)))
				(text "PT8810"
					(at 0 0 0)
					(effects (font (size 1.27 1.27) (bold yes)))
				)
			)
			(symbol "PT8810_TSSOP8_1_1"
			(pin passive line
				(at -20.32 -10.16 0)
				(length 5.08)
				(name "S1" (effects (font (size 1.016 1.016))))
				(number "1" (effects (font (size 1.016 1.016))))
			)
			(pin passive line
				(at -20.32 -5.08 0)
				(length 5.08)
				(name "D1" (effects (font (size 1.016 1.016))))
				(number "5" (effects (font (size 1.016 1.016))))
			)
			(pin passive line
				(at -20.32 -5.08 0)
				(length 5.08)
				(name "D1" (effects (font (size 1.016 1.016))))
				(number "6" (effects (font (size 1.016 1.016))))
			)
			(pin input line
				(at -20.32 5.08 0)
				(length 5.08)
				(name "G1" (effects (font (size 1.016 1.016))))
				(number "2" (effects (font (size 1.016 1.016))))
			)
			(pin passive line
				(at 20.32 -10.16 180)
				(length 5.08)
				(name "S2" (effects (font (size 1.016 1.016))))
				(number "3" (effects (font (size 1.016 1.016))))
			)
			(pin passive line
				(at 20.32 -5.08 180)
				(length 5.08)
				(name "D2" (effects (font (size 1.016 1.016))))
				(number "7" (effects (font (size 1.016 1.016))))
			)
			(pin passive line
				(at 20.32 -5.08 180)
				(length 5.08)
				(name "D2" (effects (font (size 1.016 1.016))))
				(number "8" (effects (font (size 1.016 1.016))))
			)
			(pin input line
				(at 20.32 5.08 180)
				(length 5.08)
				(name "G2" (effects (font (size 1.016 1.016))))
				(number "4" (effects (font (size 1.016 1.016))))
			)
			)
		)
'@
$sch = $sch.Substring(0, $oldSymbol.Start) + $newSymbol + $sch.Substring($oldSymbol.End + 1)

$sch = $sch.Replace('(lib_id "Daughterboard:PROT_FET_PAIR")', '(lib_id "Daughterboard:PT8810_TSSOP8")')
$sch = $sch.Replace('(property "Value" "CSD83325L"', '(property "Value" "PT8810 dual N-MOSFET"')
$sch = $sch.Replace('(property "Footprint" "Daughterboard:CSD83325L_YJE0006A"', '(property "Footprint" "Package_SO:TSSOP-8_4.4x3mm_P0.65mm"')
$sch = $sch.Replace('(property "Purchase Link" "https://www.digikey.com/en/products/result?keywords=CSD83325L"', '(property "Purchase Link" "https://jlcpcb.com/partdetail/PT-PT8810/C3019811"')
$sch = $sch.Replace('(property "Value" "TBD 1%"', '(property "Value" "43R 1W CBSET"')
$sch = $sch.Replace('(property "Footprint" "Resistor_SMD:R_0603_1608Metric"', '(property "Footprint" "Resistor_SMD:R_2512_6332Metric"')
$sch = $sch.Replace('(property "Purchase Link" "https://www.digikey.com/en/products/result?keywords=0603+resistor+1%25"', '(property "Purchase Link" "https://jlcpcb.com/partdetail/C38957"')
$sch = $sch.Replace('(global_label "BATT_RAW_N"`r`n`t`t(shape bidirectional)`r`n`t`t(at 280.67 191.77 0)', '(global_label "Q1_DRAIN_COMMON"`r`n`t`t(shape bidirectional)`r`n`t`t(at 280.67 191.77 0)')
$sch = $sch.Replace('(global_label "BATT_RAW_N"`n`t`t(shape bidirectional)`n`t`t(at 280.67 191.77 0)', '(global_label "Q1_DRAIN_COMMON"`n`t`t(shape bidirectional)`n`t`t(at 280.67 191.77 0)')
$sch = $sch.Replace('(global_label "GND"`r`n`t`t(shape bidirectional)`r`n`t`t(at 331.47 191.77 0)', '(global_label "Q1_DRAIN_COMMON"`r`n`t`t(shape bidirectional)`r`n`t`t(at 331.47 191.77 0)')
$sch = $sch.Replace('(global_label "GND"`n`t`t(shape bidirectional)`n`t`t(at 331.47 191.77 0)', '(global_label "Q1_DRAIN_COMMON"`n`t`t(shape bidirectional)`n`t`t(at 331.47 191.77 0)')

$sch = $sch.Replace('(property "Value" "BQ25887RGE 2S charger"', '(property "Value" "BQ25887RGER 2S charger"')
$sch = $sch.Replace('(property "Purchase Link" "https://www.digikey.com/en/products/result?keywords=BQ25887RGE"', '(property "Purchase Link" "https://jlcpcb.com/partdetail/TexasInstruments-BQ25887RGER/C2761614"')
$sch = $sch.Replace('(property "Value" "AP63200WU 5V Buck"', '(property "Value" "AP63200WU-7 5V Buck"')
$sch = $sch.Replace('(property "Value" "AP63200WU 3V3 Buck"', '(property "Value" "AP63200WU-7 3V3 Buck"')
$sch = $sch.Replace('(property "Purchase Link" "https://www.digikey.com/en/products/result?keywords=AP63200WU"', '(property "Purchase Link" "https://jlcpcb.com/partdetail/DiodesIncorporated-AP63200WU7/C2071868"')
$sch = $sch.Replace('(property "Value" "ADS1115IDGS 0x48"', '(property "Value" "ADS1115IDGSR 0x48"')
$sch = $sch.Replace('(property "Value" "ADS1115IDGS 0x49"', '(property "Value" "ADS1115IDGSR 0x49"')
$sch = $sch.Replace('(property "Purchase Link" "https://www.digikey.com/en/products/result?keywords=ADS1115IDGS"', '(property "Purchase Link" "https://jlcpcb.com/partdetail/TexasInstruments-ADS1115IDGSR/C37593"')
$sch = $sch.Replace('(property "Value" "UART JST-GH 1.25mm"', '(property "Value" "BM05B-GHS-TBT UART JST-GH top-entry 1.25mm"')
$sch = $sch.Replace('(property "Value" "I2C male header 2.54mm"', '(property "Value" "I2C SMD male header 2.54mm"')
$sch = $sch.Replace('(property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"', '(property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical_SMD_Pin1Left"')
$sch = $sch.Replace('(property "Value" "Deep sleep pushbutton header"', '(property "Value" "Deep sleep SMD pushbutton header"')
$sch = $sch.Replace('(property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"', '(property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical_SMD_Pin1Left"')

Set-Content -LiteralPath $SchPath -Value $sch -Encoding utf8

"Removed $($routeResult.Segments) nearby routed segments and $($routeResult.Vias) nearby vias around changed footprints."
