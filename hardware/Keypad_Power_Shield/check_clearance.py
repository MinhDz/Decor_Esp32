import math

components = [
    ("MH1", "MH", 4.0, 4.0, 7.4, 7.4),
    ("MH2", "MH", 86.0, 4.0, 7.4, 7.4),
    ("MH3", "MH", 4.0, 56.0, 7.4, 7.4),
    ("MH4", "MH", 86.0, 56.0, 7.4, 7.4),

    ("J_BAT", "JST", 74.0, 7.0, 6.0, 6.0),
    ("J_ESP", "HDR", 84.0, 19.35, 3.0, 15.5),

    ("J_MHCD42", "HDR", 8.0, 19.35, 3.0, 15.5),
    ("R_CHG1", "R", 18.0, 14.0, 10.0, 3.5),
    ("R_CHG2", "R", 18.0, 20.0, 10.0, 3.5),
    ("C_CHG", "C", 18.0, 26.0, 5.5, 3.0),
    
    ("SW_PWR", "SW_SLD", 26.0, 54.0, 9.0, 5.0),
    ("R_LED", "R", 20.0, 47.0, 10.0, 3.5),
    ("D_PWR", "LED", 12.0, 47.0, 4.0, 4.0),

    ("R_BAT1", "R", 83.0, 33.0, 10.0, 3.5),
    ("R_BAT2", "R", 83.0, 39.0, 10.0, 3.5),
    ("C_BAT", "C", 83.0, 46.0, 5.5, 3.0),

    ("R_PU", "R", 42.0, 9.5, 10.0, 3.5),
    ("C_KEY", "C", 32.0, 9.5, 5.5, 3.0),

    ("R_UP", "R", 56.0, 9.5, 10.0, 3.5),
    ("SW_UP", "SW", 56.0, 17.0, 7.5, 7.5),

    ("R_LEFT", "R", 42.0, 22.5, 10.0, 3.5),
    ("SW_LEFT", "SW", 42.0, 30.0, 7.5, 7.5),

    ("SW_OK", "SW", 56.0, 30.0, 7.5, 7.5),

    ("R_RIGHT", "R", 70.0, 22.5, 10.0, 3.5),
    ("SW_RIGHT", "SW", 70.0, 30.0, 7.5, 7.5),

    ("R_DOWN", "R", 56.0, 37.5, 10.0, 3.5),
    ("SW_DOWN", "SW", 56.0, 45.0, 7.5, 7.5),

    ("R_MENU", "R", 42.0, 44.5, 10.0, 3.5),
    ("SW_MENU", "SW", 42.0, 52.0, 7.5, 7.5),

    ("R_EXIT", "R", 70.0, 44.5, 10.0, 3.5),
    ("SW_EXIT", "SW", 70.0, 52.0, 7.5, 7.5),
]

print("=== Checking Board Boundaries (0..90, 0..60) ===")
for ref, ftype, cx, cy, w, h in components:
    min_x, max_x = cx - w/2, cx + w/2
    min_y, max_y = cy - h/2, cy + h/2
    if min_x < 0 or max_x > 90 or min_y < 0 or max_y > 60:
        print(f"OUT OF BOUNDS: {ref} [{min_x:.1f}, {max_x:.1f}] x [{min_y:.1f}, {max_y:.1f}]")
print("Boundary check completed.")

print("\n=== Checking Component Pair Overlaps ===")
overlaps = 0
for i in range(len(components)):
    for j in range(i + 1, len(components)):
        r1, t1, x1, y1, w1, h1 = components[i]
        r2, t2, x2, y2, w2, h2 = components[j]
        dx = abs(x1 - x2)
        dy = abs(y1 - y2)
        min_dx = (w1 + w2) / 2
        min_dy = (h1 + h2) / 2
        if dx < min_dx and dy < min_dy:
            print(f"OVERLAP: {r1} and {r2} (dx={dx:.1f} < {min_dx:.1f}, dy={dy:.1f} < {min_dy:.1f})")
            overlaps += 1

if overlaps == 0:
    print("ZERO OVERLAPS! All 31 components have full clearance!")
else:
    print(f"Found {overlaps} overlaps.")
