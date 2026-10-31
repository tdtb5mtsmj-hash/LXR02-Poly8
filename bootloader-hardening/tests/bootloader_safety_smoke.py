from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "Core/Src/main.c").read_text()
flash = (root / "Core/Src/flash_if.c").read_text()
header = (root / "Core/Inc/flash_if.h").read_text()

required_main = [
    "const FSIZE_t actualFileSize = f_size",
    "actualFileSize != (FSIZE_t)(HEADER_SIZE + imageHeader.datasize)",
    "fr != FR_OK || bytesRead != HEADER_SIZE",
    "fr != FR_OK || bytesRead != requested",
    "checksum != 0xFFU",
    "application_vector_valid(vectors[0], vectors[1])",
    "eraseInit.NbSectors = 7U",
    "eraseStatus != HAL_OK || sectorError != 0xFFFFFFFFU",
    "HAL_FLASH_Lock();",
]
for token in required_main:
    assert token in main, f"missing updater safety check: {token}"

assert main.index("application_vector_valid(vectors[0], vectors[1])") < main.index("HAL_FLASHEx_Erase")
assert main.index("checksum != 0xFFU") < main.index("HAL_FLASHEx_Erase")
assert "filesize = imageHeader.datasize" not in main
assert "bytesRead/4" not in main
assert "NbSectors\t \t= 6" not in main

required_flash = [
    "USER_FLASH_END_ADDRESS_EXCLUSIVE",
    "if (FlashAddress == NULL || Data == NULL",
    "DataLength > ((USER_FLASH_END_ADDRESS_EXCLUSIVE - *FlashAddress) / 4U)",
    "if (*(volatile uint32_t *)(*FlashAddress) != value)",
    "return (status == HAL_OK && sectorError == 0xFFFFFFFFU) ? 0 : 1;",
]
for token in required_flash:
    assert token in flash, f"missing flash safety check: {token}"

assert "#define USER_FLASH_END_ADDRESS_EXCLUSIVE 0x08100000UL" in header
assert "#define APPLICATION_ADDRESS   ADDR_FLASH_SECTOR_1" in header
print("Hardened SD updater static safety checks passed.")
