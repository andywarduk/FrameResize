rm -rf Test/Dst/* >/dev/null 2>&1
mkdir Test/Dst/Landscape
mkdir Test/Dst/Portrait
mkdir Test/Dst/Square
Src/FrameResize -v Test/Src Test/Dst/Landscape 800x480
Src/FrameResize -v Test/Src Test/Dst/Portrait 480x800
Src/FrameResize -v Test/Src Test/Dst/Square 480x480
