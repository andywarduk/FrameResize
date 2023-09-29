rm -rf Test/Dst/* >/dev/null 2>&1
mkdir -p Test/Dst/Landscape
mkdir -p Test/Dst/Portrait
mkdir -p Test/Dst/Square

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
