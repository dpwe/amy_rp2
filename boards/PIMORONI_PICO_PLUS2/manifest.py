include("$(PORT_DIR)/boards/manifest.py")
# AMY's Python package, unchanged; it finds our c_amy C module.
# (test.py and headers.py are desktop-only and large.)
package(
    "amy",
    files=["__init__.py", "constants.py", "examples.py", "fm.py", "juno.py",
           "piano.py", "piano_params.py", "timing.py", "xanadu.py"],
    base_path="$(MPY_DIR)/../amy",
    opt=2,
)
