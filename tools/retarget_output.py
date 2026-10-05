import glob, io, sys

pat_old = [
    ("$(SolutionDir).." + "\\" + "build" + "\\" + "bin" + "\\", "$(SolutionDir).." + "\\" + ".target" + "\\" + "bin" + "\\"),
    ("$(SolutionDir).." + "\\" + "bin" + "\\", "$(SolutionDir).." + "\\" + ".target" + "\\" + "bin" + "\\"),
]
files = sorted(glob.glob('build/VisualStudio/*.vcxproj') + glob.glob('locale/*/*.vcxproj'))
changed = []
for f in files:
    s = io.open(f, encoding='utf-8-sig').read()
    n = s
    for a, b in pat_old:
        n = n.replace(a, b)
    # the VS solution lives one directory deeper (build/VisualStudio/), so its
    # $(SolutionDir).. lands in build/ rather than the repo root.
    if 'VisualStudio' in f:
        n = n.replace("$(SolutionDir)..\\.target", "$(SolutionDir)..\\..\\.target")
    # idempotence guard: collapse double .target if rerun
    if n != s:
        io.open(f, 'w', encoding='utf-8-sig', newline='').write(n)
        changed.append(f)
print(len(files), 'projects scanned')
print(len(changed), 'rewritten')
for c in changed:
    print(' ', c)