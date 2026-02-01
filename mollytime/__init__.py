import os
import site

from pathlib import Path

# Meson-python will stick packaged wheel DLLs in a top-level site-packages location.
# Look through all site-packages directories to see if we can find it.
all_site_packages_dirs = site.getsitepackages() + [ site.getusersitepackages() ]
for site_package_dir in all_site_packages_dirs:
    maybe_library_dir = Path(site_package_dir).resolve() / ".mollytime.mesonpy.libs"
    if maybe_library_dir.exists():
        _ = os.add_dll_directory(str(maybe_library_dir))