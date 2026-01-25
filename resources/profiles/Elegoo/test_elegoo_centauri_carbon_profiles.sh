#!/bin/bash
# Test script for Elegoo Centauri Carbon buildplate STL

echo "=== Testing Centauri Carbon Buildplate STL ==="

python3 << 'PYTEST'
from stl import mesh
import numpy as np
import sys

def test_stl(filepath):
    print(f"Testing: {filepath}")
    
    try:
        m = mesh.Mesh.from_file(filepath)
    except Exception as e:
        print(f"  FAIL: Cannot load STL - {e}")
        return False
    
    errors = []
    
    # Test 1: Has reasonable number of triangles
    if len(m.vectors) < 100:
        errors.append(f"Too few triangles: {len(m.vectors)} (expected > 100)")
    
    # Test 2: X range is approximately 256mm centered
    x_min, x_max = m.vectors[:,:,0].min(), m.vectors[:,:,0].max()
    if x_min > -125 or x_max < 125:
        errors.append(f"X range too small: {x_min:.1f} to {x_max:.1f} (expected ~±128)")
    
    # Test 3: Y range shows handle at back (Y > 130)
    y_max_all = m.vectors[:,:,1].max()
    if y_max_all < 130:
        errors.append(f"No handle detected: Y max is {y_max_all:.1f} (expected > 130)")
    
    # Test 4: Y range shows corner tabs at front (Y < -130)
    y_min_all = m.vectors[:,:,1].min()
    if y_min_all > -130:
        errors.append(f"No corner tabs detected: Y min is {y_min_all:.1f} (expected < -130)")
    
    # Test 5: Handle is centered (triangles ENTIRELY in back area should be centered)
    handle_tris = [tri for tri in m.vectors if tri[:,1].min() > 128]  # entirely in back
    if handle_tris:
        handle_x_centers = [tri[:,0].mean() for tri in handle_tris]
        off_center = [x for x in handle_x_centers if abs(x) > 40]
        if len(off_center) > len(handle_tris) * 0.15:
            errors.append(f"Handle text detected: {len(off_center)} triangles off-center")
    
    # Test 6: No text in front middle - triangles ENTIRELY in front should be at corners
    # "Entirely in front" = both min and max Y < -128
    front_only_tris = [tri for tri in m.vectors if tri[:,1].max() < -128]
    if front_only_tris:
        front_x_centers = [tri[:,0].mean() for tri in front_only_tris]
        middle_tris = [x for x in front_x_centers if -100 < x < 100]
        if len(middle_tris) > 5:
            errors.append(f"Front text detected: {len(middle_tris)} triangles entirely in front middle")
    
    # Test 7: Z thickness is reasonable (0.3 to 2mm)
    z_min, z_max = m.vectors[:,:,2].min(), m.vectors[:,:,2].max()
    thickness = z_max - z_min
    if thickness < 0.3 or thickness > 3:
        errors.append(f"Bad thickness: {thickness:.1f}mm (expected 0.5-2mm)")
    
    if errors:
        print("  FAILED:")
        for e in errors:
            print(f"    - {e}")
        return False
    else:
        print(f"  PASSED: {len(m.vectors)} tris, X=[{x_min:.1f},{x_max:.1f}], Y=[{y_min_all:.1f},{y_max_all:.1f}]")
        return True

# Test all three locations
locations = [
    "/Users/tom/Development/BambuStudio/resources/profiles/Elegoo/elegoo_centuri_carbon_buildplate_model.stl",
    "/Users/tom/Development/BambuStudio/build/arm64/src/Release/BambuStudio.app/Contents/Resources/profiles/Elegoo/elegoo_centuri_carbon_buildplate_model.stl",
    "/Users/tom/Library/Application Support/BambuStudio/system/Elegoo/elegoo_centuri_carbon_buildplate_model.stl"
]

all_passed = True
for loc in locations:
    if not test_stl(loc):
        all_passed = False

print()
if all_passed:
    print("=== ALL TESTS PASSED ===")
    sys.exit(0)
else:
    print("=== SOME TESTS FAILED ===")
    sys.exit(1)
PYTEST
