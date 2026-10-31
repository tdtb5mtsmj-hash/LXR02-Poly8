from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
parameters = (root / "Core/Inc/Parameters.h").read_text()
preset = (root / "Core/Src/Preset/presetManager.c").read_text()

start = parameters.index("PAR_BEGINNING_OF_GLOBALS,") + len("PAR_BEGINNING_OF_GLOBALS,")
end = parameters.index("NUM_PARAMS", start)
global_names = re.findall(
    r"^\s*(PAR_[A-Z0-9_]+)\s*(?:=[^,\n]+)?\s*,",
    parameters[start:end],
    re.MULTILINE,
)
assert len(global_names) == 134, f"expected 134 current globals, got {len(global_names)}"

constants = {
    "LEGACY_GLOBAL_PARAM_COUNT": 38,
    "LEGACY_ALL_GLOBAL_BLOCK_SIZE": 64,
    "V3_GLOBAL_PARAM_COUNT": 110,
    "V3_ALL_GLOBAL_BLOCK_SIZE": 128,
    "CURRENT_ALL_GLOBAL_BLOCK_SIZE": 160,
}
for name, value in constants.items():
    assert re.search(rf"#define\s+{name}\s+{value}u\b", preset), (
        f"{name} must remain {value}"
    )

assert "(version >= 3 ? V3_GLOBAL_PARAM_COUNT : LEGACY_GLOBAL_PARAM_COUNT)" in preset
assert "(version >= 3 ? V3_ALL_GLOBAL_BLOCK_SIZE : LEGACY_ALL_GLOBAL_BLOCK_SIZE)" in preset
assert "activeParameters[PAR_POLY8_VOL] = 31;" in preset
assert "const FRESULT readResult = f_read((FIL*)&SDFile, globalData, numGlobals, &bytesRead);" in preset
assert "memcpy(&activeParameters[PAR_BEGINNING_OF_GLOBALS], globalData, numGlobals);" in preset
print("Poly8 preset layout compatibility passed: v1/v2=38/64, v3=110/128, v4=134/160.")
