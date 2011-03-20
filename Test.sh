rm -rf Test/Dst/* >/dev/null 2>&1
mkdir Test/Dst/Landscape
mkdir Test/Dst/Portrait
mkdir Test/Dst/Square

echo
echo Landscape test
echo ==============
echo
Src/FrameResize -v Test/Src Test/Dst/Landscape 800x480

echo
echo Portrait test
echo =============
echo
Src/FrameResize -v Test/Src Test/Dst/Portrait 480x800

echo
echo Square test
echo ===========
echo
Src/FrameResize -v Test/Src Test/Dst/Square 480x480
