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

# AMYboard firmware Python, unchanged from tulipcc (scripts/sync_tulipcc_py.sh),
# plus this board's _tulip and boot sequence.
freeze("$(BOARD_DIR)/../../py/tulipcc")
module("_tulip.py", base_path="$(BOARD_DIR)/../../py")
module("amyboard_boot.py", base_path="$(BOARD_DIR)/../../py")
