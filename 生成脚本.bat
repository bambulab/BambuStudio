mkdir install_dir
mkdir build
cd build
echo "%PATH%" | findstr /C:"%VSINSTALLDIR%" 1 >nul
if errorlevel 1 (
	echo adding VsDevCmd.bat
	call "%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat"
)
cmake F:\slicer\bamboo_slicer -G "Visual Studio 16 2019" -DCMAKE_PREFIX_PATH="F:/slicer/bamboo_slicer_dep/usr/local" -DFDAL_PATH="F:/slicer/bamboo_share/fdal" -DCMAKE_INSTALL_PREFIX="../install_dir" -DSLIC3R_ENC_CHECK=OFF
